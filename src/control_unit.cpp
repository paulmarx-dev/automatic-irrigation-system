#include <Arduino.h>
#include <WiFi.h>
#include "common_config.h"
#include "esp_now_helpers.h"
#include "pairing.h"
#include "pairing_nvs.h"
#include "telemetry.h"
#include "protocol.h"
#include "sensors.h"
#include "leds.h"
#include "button.h"
#include "app_log.h"
#include "sleep_logic.h"

#if defined(DEVICE_ROLE_CONTROL)

static const char *DEVICE_ROLE = "CONTROL";
static const char *DEVICE_ID = "2";
static const uint32_t MULTIPRESS_WINDOW_MS = 1400;
static const uint32_t MEASUREMENT_INTERVAL_MS = 1000;
static constexpr uint8_t CONTROL_MOTOR_PIN = 0;
static constexpr bool CONTROL_MOTOR_ACTIVE_HIGH = true;
static bool s_autoJoinTriggered = false;
static bool s_manualIrrigationActive = false;
static uint32_t s_motorSafetyDeadlineMs = 0;
static uint64_t s_currentLeaseId = 0;
static uint64_t s_lastExpiredLeaseId = 0;
static bool s_headSyncPending = false;
static uint32_t s_headSyncDeadlineMs = 0;
static uint32_t s_nextHeadSyncRequestMs = 0;
static uint16_t s_controlRemoteSeq = 0;
static uint8_t s_pressCount = 0;
static uint32_t s_pressWindowDeadlineMs = 0;
static SensorMeasurement s_latestMeasurement{};
static bool s_haveMeasurement = false;
static uint32_t s_lastMeasurementMs = 0;
static bool s_lowBatteryLockout = false;
static uint32_t s_battStopBelowSinceMs = 0;
static uint32_t s_battResumeAboveSinceMs = 0;

struct PressArbEvents {
  bool single;
  bool triple;
  bool debug;
};

static void triggerIrrigationLedIfDebug(LedMode mode)
{
  if (!buttonIsDebugEnabled()) {
    return;
  }
  ledsTriggerOnce(mode);
}

static void applyMotorState(bool enabled)
{
  const uint8_t level =
      (enabled == CONTROL_MOTOR_ACTIVE_HIGH) ? HIGH : LOW;
  digitalWrite(CONTROL_MOTOR_PIN, level);
}

static void setManualIrrigationActive(bool active, uint32_t nowMs)
{
  s_manualIrrigationActive = active;
  telemetrySetNodeStatusFlags(FLAG_IRRIGATION_ACTIVE, active);
  if (active) {
    s_motorSafetyDeadlineMs = nowMs + CONTROL_MOTOR_MAX_RUN_CAP_MS;
    applyMotorState(true);
    return;
  }

  s_motorSafetyDeadlineMs = 0;
  applyMotorState(false);
}

static void setLowBatteryLockout(bool enabled)
{
  if (s_lowBatteryLockout == enabled) {
    return;
  }

  s_lowBatteryLockout = enabled;
  telemetrySetNodeStatusFlags(FLAG_NODE_LOW_BATTERY_LOCKOUT, s_lowBatteryLockout);

  if (enabled) {
    Serial.println("CONTROL: irrigation lockout ENABLED (low battery)");
  } else {
    Serial.println("CONTROL: irrigation lockout CLEARED (battery recovered)");
  }
}

static void batteryLockoutTick(uint32_t nowMs)
{
  if (!s_haveMeasurement) {
    return;
  }

  const uint16_t battMv = s_latestMeasurement.batteryEstMv;

  if (s_manualIrrigationActive) {
    if (battMv < CONTROL_BATT_STOP_NOW_MV) {
      if (s_battStopBelowSinceMs == 0) {
        s_battStopBelowSinceMs = nowMs;
      } else if ((nowMs - s_battStopBelowSinceMs) >= CONTROL_BATT_LOCKOUT_CONFIRM_MS) {
        setManualIrrigationActive(false, nowMs);
        setLowBatteryLockout(true);
        Serial.print("CONTROL: irrigation forced OFF, battery low battMv=");
        Serial.println((unsigned long)battMv);
      }
    } else {
      s_battStopBelowSinceMs = 0;
    }
  } else {
    s_battStopBelowSinceMs = 0;
  }

  if (!s_lowBatteryLockout && battMv < CONTROL_BATT_BLOCK_START_MV) {
    setLowBatteryLockout(true);
    s_battResumeAboveSinceMs = 0;
    Serial.print("CONTROL: lockout threshold reached battMv=");
    Serial.println((unsigned long)battMv);
  }

  if (s_lowBatteryLockout) {
    if (battMv >= CONTROL_BATT_RESUME_OK_MV) {
      if (s_battResumeAboveSinceMs == 0) {
        s_battResumeAboveSinceMs = nowMs;
      } else if ((nowMs - s_battResumeAboveSinceMs) >= CONTROL_BATT_LOCKOUT_CONFIRM_MS) {
        setLowBatteryLockout(false);
        s_battResumeAboveSinceMs = 0;
        Serial.print("CONTROL: battery recovered battMv=");
        Serial.println((unsigned long)battMv);
      }
    } else {
      s_battResumeAboveSinceMs = 0;
    }
  } else {
    s_battResumeAboveSinceMs = 0;
  }
}

static void armHeadSyncWindow(uint32_t nowMs)
{
  if (!pairingNodeIsPaired()) {
    s_headSyncPending = false;
    s_headSyncDeadlineMs = 0;
    s_nextHeadSyncRequestMs = 0;
    return;
  }

  s_headSyncPending = true;
  s_headSyncDeadlineMs = nowMs + CONTROL_HEAD_SYNC_BOOT_WINDOW_MS;
  s_nextHeadSyncRequestMs = 0;
}

static bool sendIrrigationStateRequestToHead(uint32_t nowMs)
{
  uint8_t headMac[6] = {0};
  if (!pairingNodeHeadMac(headMac)) {
    return false;
  }

  MsgRemoteButton command{};
  command.hdr.ver = PROTO_VER;
  command.hdr.type = MSG_REMOTE_BUTTON;
  command.hdr.seq = ++s_controlRemoteSeq;
  command.hdr.nodeId = pairingNodeId();
  command.action = REMOTE_BUTTON_IRRIGATION_STATE_REQUEST;
  command.cmdId = 0;

  (void)espnowEnsurePeer(headMac, ESPNOW_CHANNEL, false);
  const bool sent = espnowSend(headMac, reinterpret_cast<const uint8_t*>(&command), sizeof(command));
  Serial.print("CONTROL: irrigation state request sent=");
  Serial.println(sent ? 1 : 0);
  s_nextHeadSyncRequestMs = nowMs + CONTROL_HEAD_SYNC_RETRY_MS;
  return sent;
}

static void headSyncTick(uint32_t nowMs)
{
  if (!s_headSyncPending) {
    return;
  }

  if (!pairingNodeIsPaired()) {
    s_headSyncPending = false;
    s_headSyncDeadlineMs = 0;
    s_nextHeadSyncRequestMs = 0;
    return;
  }

  if ((int32_t)(nowMs - s_headSyncDeadlineMs) >= 0) {
    s_headSyncPending = false;
    Serial.println("CONTROL: irrigation state request timeout, keeping local fail-safe state");
    return;
  }

  if (s_nextHeadSyncRequestMs == 0 || (int32_t)(nowMs - s_nextHeadSyncRequestMs) >= 0) {
    (void)sendIrrigationStateRequestToHead(nowMs);
  }
}

static PressArbEvents processMultipressArbitration(bool shortPress, uint32_t now)
{
  PressArbEvents events{false, false, false};

  if (shortPress) {
    if (s_pressCount < 255) {
      s_pressCount++;
    }
    s_pressWindowDeadlineMs = now + MULTIPRESS_WINDOW_MS;
  }

  if (s_pressCount == 0) {
    return events;
  }
  if ((int32_t)(now - s_pressWindowDeadlineMs) < 0) {
    return events;
  }

  if (s_pressCount >= 5) {
    Serial.printf("BTN_ARB: window closed, count=%u -> debug\n", s_pressCount);
    events.debug = true;
  } else if (s_pressCount == 3) {
    Serial.println("BTN_ARB: window closed, count=3 -> reserved");
    events.triple = true;
  } else if (s_pressCount == 1) {
    Serial.println("BTN_ARB: window closed, count=1 -> single");
    events.single = true;
  } else {
    Serial.printf("BTN_ARB: window closed, count=%u -> ignored\n", s_pressCount);
  }

  s_pressCount = 0;
  return events;
}

static void sendControlCommandAck(const uint8_t* dstMac, uint16_t cmdId, uint8_t action, uint8_t status)
{
  if (!dstMac || !pairingNodeIsPaired()) {
    return;
  }

  MsgCommandAck ack{};
  ack.hdr.ver = PROTO_VER;
  ack.hdr.type = MSG_COMMAND_ACK;
  ack.hdr.seq = ++s_controlRemoteSeq;
  ack.hdr.nodeId = pairingNodeId();
  ack.cmdId = cmdId;
  ack.action = action;
  ack.status = status;
  ack.irrigationState = s_manualIrrigationActive ? IRRIGATION_STATE_RUN : IRRIGATION_STATE_OFF;

  uint32_t remainingSec = 0;
  if (s_manualIrrigationActive && s_motorSafetyDeadlineMs != 0) {
    const uint32_t nowMs = millis();
    if ((int32_t)(s_motorSafetyDeadlineMs - nowMs) > 0) {
      remainingSec = static_cast<uint32_t>(s_motorSafetyDeadlineMs - nowMs) / 1000UL;
    }
  }
  ack.remainingSec = remainingSec;

  (void)espnowEnsurePeer(dstMac, ESPNOW_CHANNEL, false);
  const bool sent = espnowSend(dstMac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
  Serial.print("CONTROL: cmd_ack sent=");
  Serial.print(sent ? 1 : 0);
  Serial.print(" cmdId=");
  Serial.print((unsigned long)cmdId);
  Serial.print(" action=");
  Serial.print((unsigned long)action);
  Serial.print(" status=");
  Serial.print((unsigned long)status);
  Serial.print(" irState=");
  Serial.print((unsigned long)ack.irrigationState);
  Serial.print(" remainingSec=");
  Serial.println((unsigned long)ack.remainingSec);
}

static bool handleRemoteCommand(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (!src_mac || !data || len < (int)sizeof(MsgHdr)) {
    return false;
  }

  const MsgHdr* hdr = reinterpret_cast<const MsgHdr*>(data);
  if (hdr->ver != PROTO_VER) {
    return false;
  }

  if (!pairingNodeIsPaired()) {
    return false;
  }

  const uint16_t localNodeId = pairingNodeId();
  if (hdr->type == MSG_IRRIGATION_STATE) {
    if (len != (int)sizeof(MsgIrrigationState)) {
      return false;
    }

    const MsgIrrigationState* state = reinterpret_cast<const MsgIrrigationState*>(data);
    if (state->hdr.nodeId != 0 && state->hdr.nodeId != localNodeId) {
      return false;
    }

    if (state->leaseId < s_currentLeaseId) {
      Serial.print("CONTROL: irrigation lease ignored stale leaseId=");
      Serial.print((unsigned long long)state->leaseId);
      Serial.print(" current=");
      Serial.println((unsigned long long)s_currentLeaseId);
      return true;
    }

    s_currentLeaseId = state->leaseId;
    s_headSyncPending = false;

    if (state->desiredState == IRRIGATION_STATE_OFF) {
      if (s_manualIrrigationActive) {
        setManualIrrigationActive(false, millis());
        Serial.println("CONTROL: irrigation OFF lease applied");
        triggerIrrigationLedIfDebug(LED_MODE_ERROR_ONCE);
      }
      return true;
    }

    if (state->desiredState != IRRIGATION_STATE_RUN) {
      return false;
    }

    if (!s_manualIrrigationActive && state->leaseId == s_lastExpiredLeaseId) {
      Serial.print("CONTROL: irrigation lease ignored expired leaseId=");
      Serial.println((unsigned long long)state->leaseId);
      return true;
    }

    const uint32_t nowMs = millis();
    uint32_t effectiveRemainingMs = state->remainingLeaseMs;
    if (effectiveRemainingMs > CONTROL_MOTOR_MAX_RUN_CAP_MS) {
      effectiveRemainingMs = CONTROL_MOTOR_MAX_RUN_CAP_MS;
    }

    if (effectiveRemainingMs == 0) {
      if (s_manualIrrigationActive) {
        setManualIrrigationActive(false, nowMs);
        Serial.println("CONTROL: irrigation RUN lease with zero remaining -> OFF");
      }
      return true;
    }

    if (!s_manualIrrigationActive && s_lowBatteryLockout) {
      Serial.println("CONTROL: irrigation RUN lease ignored (low battery lockout)");
      return true;
    }

    setManualIrrigationActive(true, nowMs);
    s_motorSafetyDeadlineMs = nowMs + effectiveRemainingMs;
    Serial.print("CONTROL: irrigation RUN lease applied leaseId=");
    Serial.print((unsigned long long)state->leaseId);
    Serial.print(" remainingMs=");
    Serial.println((unsigned long)effectiveRemainingMs);
    triggerIrrigationLedIfDebug(LED_MODE_SUCCESS_ONCE);
    return true;
  }

  if (hdr->type == MSG_REMOTE_BUTTON) {
    if (len != (int)sizeof(MsgRemoteButton)) {
      return false;
    }

    const MsgRemoteButton* cmd = reinterpret_cast<const MsgRemoteButton*>(data);
    if (cmd->hdr.nodeId != 0 && cmd->hdr.nodeId != localNodeId) {
      return false;
    }

    sendControlCommandAck(src_mac, cmd->cmdId, cmd->action, COMMAND_ACK_STATUS_RECEIVED);

    if (cmd->action == REMOTE_BUTTON_IRRIGATION_START) {
      s_headSyncPending = false;
      if (!s_manualIrrigationActive) {
        if (s_lowBatteryLockout) {
          Serial.println("CONTROL: irrigation START command ignored (low battery lockout)");
          sendControlCommandAck(src_mac, cmd->cmdId, cmd->action, COMMAND_ACK_STATUS_REJECTED);
          return true;
        }
        setManualIrrigationActive(true, millis());
        s_currentLeaseId = (s_currentLeaseId == 0xFFFFFFFFFFFFFFFFull) ? 1ull : (s_currentLeaseId + 1ull);
        Serial.println("CONTROL: irrigation START command applied (legacy)");
        triggerIrrigationLedIfDebug(LED_MODE_SUCCESS_ONCE);
      }
      sendControlCommandAck(src_mac, cmd->cmdId, cmd->action, COMMAND_ACK_STATUS_APPLIED);
      return true;
    }

    if (cmd->action == REMOTE_BUTTON_IRRIGATION_STOP) {
      s_headSyncPending = false;
      if (s_manualIrrigationActive) {
        setManualIrrigationActive(false, millis());
        s_currentLeaseId = (s_currentLeaseId == 0xFFFFFFFFFFFFFFFFull) ? 1ull : (s_currentLeaseId + 1ull);
        Serial.println("CONTROL: irrigation STOP command applied (legacy)");
        triggerIrrigationLedIfDebug(LED_MODE_ERROR_ONCE);
      }
      sendControlCommandAck(src_mac, cmd->cmdId, cmd->action, COMMAND_ACK_STATUS_APPLIED);
      return true;
    }

    if (cmd->action == REMOTE_BUTTON_IRRIGATION_STATE_REQUEST) {
      sendControlCommandAck(src_mac, cmd->cmdId, cmd->action, COMMAND_ACK_STATUS_APPLIED);
      return true;
    }
  }

  return false;
}

static void onRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (pairingOnRecv(src_mac, data, len)) {
    return;
  }
  if (handleRemoteCommand(src_mac, data, len)) {
    return;
  }
  telemetryOnRecv(src_mac, data, len);
}

static void onSend(const uint8_t* dst_mac, bool success)
{
  (void)dst_mac;
  (void)success;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  setupSensors();

  Serial.println();
  Serial.println("Automatic Irrigation Control boot");
  Serial.print("Role: ");
  Serial.print(DEVICE_ROLE);
  Serial.print(", Device ID: ");
  Serial.println(DEVICE_ID);

  Serial.println("CONTROL: using factory STA MAC");

  if (!espnowInit(ESPNOW_CHANNEL, onRecv, onSend)) {
    Serial.println("espnowInit() failed");
    while (true) { delay(1000); }
  }

  pairingInitNode(ROLE_CONTROL);

  uint8_t restoredHeadMac[6] = {0};
  uint16_t restoredNodeId = 0;
  if (pairingNvsLoadNode(ROLE_CONTROL, restoredHeadMac, &restoredNodeId)) {
    pairingNodeRestorePairedHead(restoredHeadMac, restoredNodeId);
    (void)espnowEnsurePeer(restoredHeadMac, ESPNOW_CHANNEL, false);
    s_autoJoinTriggered = true;
    Serial.printf("PAIRING(NODE): restored pair from NVS nodeId=%u\n", (unsigned)restoredNodeId);
    armHeadSyncWindow(millis());
  }

  logStartupCommon("CONTROL", true, pairingNodeIsPaired());
  telemetryInit();
  telemetrySetNodeStatusFlags(FLAG_NODE_LOW_BATTERY_LOCKOUT, false);

  pinMode(CONTROL_MOTOR_PIN, OUTPUT);
  setManualIrrigationActive(false, millis());

  ledsInit(LED_DEFAULT_CONFIG.pin, LED_DEFAULT_CONFIG.activeHigh);
  buttonInit(BUTTON_CONTROL_CONFIG.pin, BUTTON_CONTROL_CONFIG.activeLow, BUTTON_CONTROL_CONFIG.usePullup);

  Serial.println("ESP-NOW ready.");
  Serial.println("USED MAC: " + WiFi.macAddress());
}

void loop() {
  const uint32_t now = millis();
  pairingTick();
  buttonTick(now);

  pairingNodeTick(now);

  if (!s_autoJoinTriggered && !pairingNodeIsPaired()) {
    pairingNodeEnterJoinMode(now);
    Serial.println("PAIRING(NODE): auto-join on boot");
    s_autoJoinTriggered = true;
  }

  headSyncTick(now);

  static bool pairStateInitialized = false;
  static bool wasPaired = false;
  static bool lastJoinModeActive = false;

  const bool isPaired = pairingNodeIsPaired();
  bool joinModeActive = pairingNodeIsInJoinMode();
  const bool rawShortPress = buttonConsumeShortPress();
  const bool longPress = buttonConsumeLongPress();
  const PressArbEvents pressEvents = processMultipressArbitration(rawShortPress, now);

  if (!pairStateInitialized) {
    wasPaired = isPaired;
    pairStateInitialized = true;
  }

  if (longPress) {
    Serial.println("PAIRING(NODE): factory reset requested");
    pairingNodeFactoryReset();
    setManualIrrigationActive(false, now);
    s_currentLeaseId = 0;
    s_lastExpiredLeaseId = 0;
    s_headSyncPending = false;
    s_headSyncDeadlineMs = 0;
    s_nextHeadSyncRequestMs = 0;
    if (!pairingNvsClearNode()) {
      Serial.println("PAIRING(NODE): NVS clear failed");
    }
    pairingNodeEnterJoinMode(now);
    s_autoJoinTriggered = true;
    joinModeActive = true;
    Serial.println("PAIRING(NODE): join window opened after factory reset");
    ledsTriggerOnce(LED_MODE_FACTORY_RESET_ONCE);
  }

  if (!wasPaired && isPaired) {
    uint8_t headMac[6] = {0};
    const uint16_t nodeId = pairingNodeId();
    if (pairingNodeHeadMac(headMac)) {
      if (!pairingNvsSaveNode(ROLE_CONTROL, headMac, nodeId)) {
        Serial.println("PAIRING(NODE): NVS save failed");
      }
    }
    armHeadSyncWindow(now);
    Serial.println("PAIRING(NODE): join success");
    ledsTriggerOnce(LED_MODE_SUCCESS_DOUBLE);
  }
  wasPaired = isPaired;

  if (!isPaired && joinModeActive && pairingNodeJoinExpired(now)) {
    pairingNodeExitJoinMode();
    joinModeActive = false;
    Serial.println("PAIRING(NODE): join window expired");
    ledsTriggerOnce(LED_MODE_ERROR_ONCE);
  }

  if (pressEvents.single) {
    if (isPaired) {
      Serial.println("PAIRING(NODE): short press ignored (already paired)");
    } else if (pairingNodeIsInJoinMode()) {
      pairingNodeExitJoinMode();
      joinModeActive = false;
      Serial.println("PAIRING(NODE): join window canceled by user");
      ledsTriggerOnce(LED_MODE_ERROR_ONCE);
    } else {
      pairingNodeEnterJoinMode(now);
      joinModeActive = true;
      Serial.println("PAIRING(NODE): join window opened");
      ledsSetBaseMode(LED_MODE_JOINING);
    }
  }

  if (pressEvents.triple) {
    Serial.println("PAIRING(NODE): triple press reserved (ignored)");
  }

  if (pressEvents.debug || buttonConsumeDebugEnabledEvent()) {
    if (pressEvents.debug) {
      buttonEnableDebug();
    }
    Serial.println("DEBUG gate: enabled for this boot");
    ledsTriggerOnce(LED_MODE_DEBUG_CONFIRM);
  }

  if (s_manualIrrigationActive && s_motorSafetyDeadlineMs != 0 && (int32_t)(now - s_motorSafetyDeadlineMs) >= 0) {
    setManualIrrigationActive(false, now);
    s_lastExpiredLeaseId = s_currentLeaseId;
    Serial.println("CONTROL: safety max run cap reached -> motor OFF");
    triggerIrrigationLedIfDebug(LED_MODE_ERROR_ONCE);
  }

  if ((now - s_lastMeasurementMs) >= MEASUREMENT_INTERVAL_MS) {
    s_lastMeasurementMs = now;
    s_latestMeasurement = measureBatteryOnly();
    s_haveMeasurement = true;
  }

  batteryLockoutTick(now);

  telemetryTickSensor(s_haveMeasurement ? &s_latestMeasurement : nullptr, s_haveMeasurement, now);

  sleepLogicSetDebugNoSleep(buttonIsDebugEnabled());
  sleepLogicSetServiceMode(pairingNodeIsInJoinMode());
  sleepLogicSetIrrigationActive(s_manualIrrigationActive);

  uint32_t sleepMs = 0;
  bool deepSleep = false;
  if (sleepLogicShouldEnterSleep(now, &sleepMs, &deepSleep)) {
    Serial.print("SLEEP: entering ");
    Serial.print(deepSleep ? "deep" : "light");
    Serial.print(" sleepMs=");
    Serial.println((unsigned long)sleepMs);
    sleepLogicEnterSleep(sleepMs, deepSleep);
  }

  const bool joinModeNow = pairingNodeIsInJoinMode();
  if (joinModeNow && !lastJoinModeActive) {
    ledsSetBaseMode(LED_MODE_JOINING);
  } else if (!joinModeNow && lastJoinModeActive) {
    ledsSetBaseMode(LED_MODE_OFF);
  }
  lastJoinModeActive = joinModeNow;

  ledsTick(now);
  delay(10);
}

#endif
