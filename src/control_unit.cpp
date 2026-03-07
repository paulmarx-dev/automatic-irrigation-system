#include <Arduino.h>
#include <WiFi.h>
#include "esp_now_helpers.h"
#include "pairing.h"
#include "pairing_nvs.h"
#include "telemetry.h"
#include "protocol.h"
#include "leds.h"
#include "button.h"
#include "app_log.h"

#if defined(DEVICE_ROLE_CONTROL)

static const char *DEVICE_ROLE = "CONTROL";
static const char *DEVICE_ID = "2";
static const uint32_t MULTIPRESS_WINDOW_MS = 1400;
static constexpr uint8_t CONTROL_MOTOR_PIN = 0;
static constexpr bool CONTROL_MOTOR_ACTIVE_HIGH = true;
static bool s_autoJoinTriggered = false;
static bool s_manualIrrigationActive = false;
static uint8_t s_pressCount = 0;
static uint32_t s_pressWindowDeadlineMs = 0;

struct PressArbEvents {
  bool single;
  bool triple;
  bool debug;
};

static void applyMotorState(bool enabled)
{
  const uint8_t level =
      (enabled == CONTROL_MOTOR_ACTIVE_HIGH) ? HIGH : LOW;
  digitalWrite(CONTROL_MOTOR_PIN, level);
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

static bool handleRemoteCommand(const uint8_t* data, int len)
{
  if (!data || len != (int)sizeof(MsgRemoteButton)) {
    return false;
  }

  const MsgRemoteButton* cmd = reinterpret_cast<const MsgRemoteButton*>(data);
  if (cmd->hdr.ver != PROTO_VER || cmd->hdr.type != MSG_REMOTE_BUTTON) {
    return false;
  }

  if (!pairingNodeIsPaired()) {
    return false;
  }

  const uint16_t localNodeId = pairingNodeId();
  if (cmd->hdr.nodeId != 0 && cmd->hdr.nodeId != localNodeId) {
    return false;
  }

  if (cmd->action == REMOTE_BUTTON_IRRIGATION_START) {
    if (!s_manualIrrigationActive) {
      s_manualIrrigationActive = true;
      applyMotorState(true);
      Serial.println("CONTROL: irrigation START command applied");
      ledsTriggerOnce(LED_MODE_SUCCESS_ONCE);
    }
    return true;
  }

  if (cmd->action == REMOTE_BUTTON_IRRIGATION_STOP) {
    if (s_manualIrrigationActive) {
      s_manualIrrigationActive = false;
      applyMotorState(false);
      Serial.println("CONTROL: irrigation STOP command applied");
      ledsTriggerOnce(LED_MODE_ERROR_ONCE);
    }
    return true;
  }

  return false;
}

static void onRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (pairingOnRecv(src_mac, data, len)) {
    return;
  }
  if (handleRemoteCommand(data, len)) {
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
  }

  logStartupCommon("CONTROL", true, pairingNodeIsPaired());
  telemetryInit();

  pinMode(CONTROL_MOTOR_PIN, OUTPUT);
  applyMotorState(false);

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
    s_manualIrrigationActive = false;
    applyMotorState(false);
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
