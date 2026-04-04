#include "telemetry.h"

#include <Arduino.h>
#include <string.h>
#include <esp_system.h>

#include "common_config.h"
#include "esp_now_helpers.h"
#include "pairing.h"
#include "protocol.h"

#if defined(DEVICE_ROLE_SENSOR) || defined(DEVICE_ROLE_CONTROL)
#include "sensors.h"
#include "button.h"
#include "leds.h"
#include "pairing_nvs.h"
#include "sleep_logic.h"
#if defined(DEVICE_ROLE_SENSOR)
#include <esp_sleep.h>
#include "sensor_remote_control.h"
#endif

#if defined(DEVICE_ROLE_SENSOR)
static constexpr uint8_t NODE_ROLE_THIS = ROLE_SENSOR;
#else
static constexpr uint8_t NODE_ROLE_THIS = ROLE_CONTROL;
#endif

static const unsigned long ACK_TIMEOUT_MS = 200;
static const uint8_t MAX_RETRIES = 3;
static const uint8_t MAX_NO_ACK_CYCLES_BEFORE_REJOIN = 3;

static uint16_t s_telemetrySeq = 0;
static uint16_t s_lastSentSeq = 0;
static bool s_waitingAck = false;
static uint32_t s_ackDeadlineMs = 0;
static uint8_t s_retryCount = 0;
static uint32_t s_lastSendStartMs = 0;
static uint8_t s_noAckCycles = 0;
static uint32_t s_nextTelemetryDueMs = 0;
static uint8_t s_nodeStatusFlags = 0;

static MsgTelemetry s_pendingTelemetry{};
static uint8_t s_pendingHeadMac[6] = {0};

#if defined(DEVICE_ROLE_SENSOR)
static uint16_t s_sensorCriticalSeq = 0;
static uint8_t s_sensorCriticalBelowCount = 0;
static bool s_sensorCriticalIntentActive = false;
static bool s_sensorCriticalSleepNow = false;
static uint8_t s_sensorCriticalRetries = 0;
static uint32_t s_sensorCriticalAckDeadlineMs = 0;
static MsgCriticalSleepIntent s_pendingCriticalIntent{};
#endif

static SleepNodeMode defaultSleepModeForRole()
{
#if defined(DEVICE_ROLE_SENSOR)
  if (SENSOR_SLEEP_MODE_DEFAULT == 5) {
    return SLEEP_NODE_MODE_DEEP;
  }
  if (SENSOR_SLEEP_MODE_DEFAULT == 1) {
    return SLEEP_NODE_MODE_LIGHT;
  }
  return SLEEP_NODE_MODE_OFF;
#else
  if (CONTROL_SLEEP_MODE_DEFAULT == 5) {
    return SLEEP_NODE_MODE_DEEP;
  }
  if (CONTROL_SLEEP_MODE_DEFAULT == 1) {
    return SLEEP_NODE_MODE_LIGHT;
  }
  return SLEEP_NODE_MODE_OFF;
#endif
}

static bool sendPendingTelemetry()
{
  (void)espnowEnsurePeer(s_pendingHeadMac, ESPNOW_CHANNEL, false);
  return espnowSend(s_pendingHeadMac, reinterpret_cast<const uint8_t*>(&s_pendingTelemetry), sizeof(s_pendingTelemetry));
}

static uint32_t randomBoundedMs(uint32_t maxExclusive)
{
  if (maxExclusive == 0) {
    return 0;
  }
  return static_cast<uint32_t>(esp_random() % maxExclusive);
}

static uint32_t deterministicNodePhaseOffsetMs()
{
  const uint16_t nodeId = pairingNodeId();
  if (nodeId == 0 || TELEMETRY_SCHEDULE_MAX_NODES == 0) {
    return 0;
  }

  uint32_t slotWidthMs = TELEMETRY_PHASE_SPREAD_MS / TELEMETRY_SCHEDULE_MAX_NODES;
  if (slotWidthMs == 0) {
    slotWidthMs = 1;
  }

  const uint32_t slot = (static_cast<uint32_t>(nodeId - 1) % TELEMETRY_SCHEDULE_MAX_NODES);
  return slot * slotWidthMs;
}

static uint32_t nextTelemetryIntervalMs()
{
  return TELEMETRY_BASE_INTERVAL_MS + randomBoundedMs(TELEMETRY_INTERVAL_JITTER_MS + 1);
}

static uint32_t retryBackoffMs(uint8_t retryIndex)
{
  const uint32_t jitter = randomBoundedMs(120);
  if (retryIndex == 0) {
    return 200 + jitter;
  }
  if (retryIndex == 1) {
    return 400 + jitter;
  }
  return 800 + jitter;
}

#if defined(DEVICE_ROLE_SENSOR)
static bool isExternalPowerReading(uint16_t batteryEstMv)
{
  return batteryEstMv <= BATTERY_EXTERNAL_POWER_MAX_MV;
}

static bool sendPendingCriticalIntent()
{
  (void)espnowEnsurePeer(s_pendingHeadMac, ESPNOW_CHANNEL, false);
  return espnowSend(s_pendingHeadMac, reinterpret_cast<const uint8_t*>(&s_pendingCriticalIntent), sizeof(s_pendingCriticalIntent));
}

static void beginSensorCriticalSleepIntent(uint16_t batteryEstMv, uint32_t nowMs)
{
  s_sensorCriticalIntentActive = true;
  s_sensorCriticalSleepNow = false;
  s_sensorCriticalRetries = 0;

  s_pendingCriticalIntent = {};
  s_pendingCriticalIntent.hdr.ver = PROTO_VER;
  s_pendingCriticalIntent.hdr.type = MSG_CRITICAL_SLEEP_INTENT;
  s_pendingCriticalIntent.hdr.seq = ++s_sensorCriticalSeq;
  if (s_sensorCriticalSeq == 0) {
    s_sensorCriticalSeq = 1;
    s_pendingCriticalIntent.hdr.seq = s_sensorCriticalSeq;
  }
  s_pendingCriticalIntent.hdr.nodeId = pairingNodeId();
  s_pendingCriticalIntent.reason = CRITICAL_SLEEP_REASON_LOW_BATTERY;
  s_pendingCriticalIntent.flags = 0;
  s_pendingCriticalIntent.thresholdMv = SENSOR_CRITICAL_SLEEP_MV;
  s_pendingCriticalIntent.batteryEstMv = batteryEstMv;
  s_pendingCriticalIntent.reserved = 0;

  const bool sent = sendPendingCriticalIntent();
  s_sensorCriticalAckDeadlineMs = nowMs + ACK_TIMEOUT_MS;

  Serial.print("CRITICAL_SLEEP intent sent=");
  Serial.print(sent ? 1 : 0);
  Serial.print(" seq=");
  Serial.print((unsigned long)s_pendingCriticalIntent.hdr.seq);
  Serial.print(" battMv=");
  Serial.println((unsigned long)batteryEstMv);
}

static void enterSensorEternalSleepNow()
{
  Serial.println("CRITICAL_SLEEP: entering eternal deep sleep");
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_start();
}
#endif

void telemetryInit()
{
  s_telemetrySeq = 0;
  s_lastSentSeq = 0;
  s_waitingAck = false;
  s_ackDeadlineMs = 0;
  s_retryCount = 0;
  s_lastSendStartMs = 0;
  s_noAckCycles = 0;
  s_nextTelemetryDueMs = 0;
  s_nodeStatusFlags = 0;
  memset(&s_pendingTelemetry, 0, sizeof(s_pendingTelemetry));
  memset(s_pendingHeadMac, 0, sizeof(s_pendingHeadMac));
#if defined(DEVICE_ROLE_SENSOR)
  s_sensorCriticalSeq = 0;
  s_sensorCriticalBelowCount = 0;
  s_sensorCriticalIntentActive = false;
  s_sensorCriticalSleepNow = false;
  s_sensorCriticalRetries = 0;
  s_sensorCriticalAckDeadlineMs = 0;
  memset(&s_pendingCriticalIntent, 0, sizeof(s_pendingCriticalIntent));
#endif
    sleepLogicInit(
  #if defined(DEVICE_ROLE_CONTROL)
    true,
    BUTTON_CONTROL_CONFIG.pin,
    BUTTON_CONTROL_CONFIG.activeLow,
  #else
    false,
    BUTTON_SENSOR_CONFIG.pin,
    BUTTON_SENSOR_CONFIG.activeLow,
  #endif
    defaultSleepModeForRole());
}

void telemetrySetNodeStatusFlags(uint8_t mask, bool enabled)
{
  if (enabled) {
    s_nodeStatusFlags = static_cast<uint8_t>(s_nodeStatusFlags | mask);
  } else {
    s_nodeStatusFlags = static_cast<uint8_t>(s_nodeStatusFlags & static_cast<uint8_t>(~mask));
  }
}

void telemetryOnRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (!src_mac || !data || len < (int)sizeof(MsgHdr)) {
    return;
  }

  const MsgHdr* hdr = reinterpret_cast<const MsgHdr*>(data);
  if (hdr->ver != PROTO_VER) {
    return;
  }

  if (hdr->type == MSG_REMOTE_BUTTON) {
#if defined(DEVICE_ROLE_SENSOR)
    if (len != (int)sizeof(MsgRemoteButton)) {
      return;
    }
    if (!pairingNodeIsPaired()) {
      return;
    }
    if (hdr->nodeId != pairingNodeId()) {
      return;
    }

    const MsgRemoteButton* cmd = reinterpret_cast<const MsgRemoteButton*>(data);
    const bool accepted = sensorHandleRemoteButtonAction(cmd->action, millis());

    Serial.print("REMOTE_BTN action=");
    Serial.print((unsigned long)cmd->action);
    Serial.print(" accepted=");
    Serial.println(accepted ? 1 : 0);
#endif
    return;
  }

  if (hdr->type == MSG_SLEEP_PLAN && len == (int)sizeof(MsgSleepPlan)) {
    if (!pairingNodeIsPaired()) {
      return;
    }

    const MsgSleepPlan* plan = reinterpret_cast<const MsgSleepPlan*>(data);
    MsgSleepAck ack{};
    const bool shouldReply = sleepLogicOnSleepPlan(*plan, pairingNodeId(), millis(), &ack);
    if (!shouldReply) {
      return;
    }

    (void)espnowEnsurePeer(src_mac, ESPNOW_CHANNEL, false);
    const bool sent = espnowSend(src_mac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
    sleepLogicNotifyAckSent(sent, millis());

    Serial.print("SLEEP_ACK sent=");
    Serial.print(sent ? 1 : 0);
    Serial.print(" planId=");
    Serial.print((unsigned long)ack.planId);
    Serial.print(" accepted=");
    Serial.print((unsigned long)ack.accepted);
    Serial.print(" reject=");
    Serial.println((unsigned long)ack.rejectReason);
    return;
  }

  if (hdr->type == MSG_SLEEP_ACK_ACK && len == (int)sizeof(MsgSleepAckAck)) {
    if (!pairingNodeIsPaired()) {
      return;
    }

    const MsgSleepAckAck* ackAck = reinterpret_cast<const MsgSleepAckAck*>(data);
    sleepLogicOnSleepAckAck(*ackAck, pairingNodeId(), millis());
    return;
  }

#if defined(DEVICE_ROLE_SENSOR)
  if (hdr->type == MSG_CRITICAL_SLEEP_ACK && len == (int)sizeof(MsgCriticalSleepAck)) {
    if (!pairingNodeIsPaired()) {
      return;
    }

    if (!s_sensorCriticalIntentActive) {
      return;
    }

    if (memcmp(src_mac, s_pendingHeadMac, 6) != 0) {
      return;
    }

    const MsgCriticalSleepAck* ack = reinterpret_cast<const MsgCriticalSleepAck*>(data);
    if (ack->hdr.nodeId != pairingNodeId()) {
      return;
    }
    if (ack->ackSeq != s_pendingCriticalIntent.hdr.seq) {
      return;
    }

    Serial.print("CRITICAL_SLEEP ack status=");
    Serial.println((unsigned long)ack->status);

    s_sensorCriticalIntentActive = false;
    s_sensorCriticalSleepNow = true;
    return;
  }
#endif

  if (hdr->type != MSG_TELEMETRY_ACK || len != (int)sizeof(MsgTelemetryAck)) {
    return;
  }

  const MsgTelemetryAck* ack = reinterpret_cast<const MsgTelemetryAck*>(data);

  if (!s_waitingAck) {
    return;
  }

  if (memcmp(src_mac, s_pendingHeadMac, 6) != 0) {
    return;
  }

  if (ack->hdr.nodeId != pairingNodeId()) {
    return;
  }

  if (ack->ackSeq != s_lastSentSeq) {
    return;
  }

  if (ack->status == TELEMETRY_ACK_STATUS_NOT_PAIRED) {
    Serial.println("PAIRING(NODE): head reports NOT_PAIRED, clearing local pairing");
    pairingInitNode(NODE_ROLE_THIS);
    if (!pairingNvsClearNode()) {
      Serial.println("PAIRING(NODE): NVS clear failed");
    }
    Serial.println("PAIRING(NODE): waiting for manual pairing action");
    ledsTriggerOnce(LED_MODE_ERROR_ONCE);
    s_waitingAck = false;
    s_noAckCycles = 0;
    sleepLogicReset();
    return;
  }

  if (ack->status != TELEMETRY_ACK_STATUS_OK) {
    Serial.print("TELEMETRY_ACK unexpected status=");
    Serial.println((unsigned long)ack->status);
    s_waitingAck = false;
    return;
  }

  const uint32_t rttMs = millis() - s_lastSendStartMs;
  s_waitingAck = false;
  s_noAckCycles = 0;
  sleepLogicOnTelemetryAck(ack->ackSeq, millis());

  if (buttonIsDebugEnabled()) {
    ledsPulseOnce(30);
  }

  Serial.print("TELEMETRY_ACK ok=1 seq=");
  Serial.print((unsigned long)ack->ackSeq);
  Serial.print(" nodeId=");
  Serial.print((unsigned long)ack->hdr.nodeId);
  Serial.print(" rttMs=");
  Serial.println((unsigned long)rttMs);
}

void telemetryTickSensor(const SensorMeasurement* measurement, bool hasMeasurement, uint32_t nowMs)
{
#if defined(DEVICE_ROLE_SENSOR)
  if (s_sensorCriticalSleepNow) {
    enterSensorEternalSleepNow();
    return;
  }
#endif

  if (!pairingNodeIsPaired()) {
    s_waitingAck = false;
    s_noAckCycles = 0;
    s_nextTelemetryDueMs = 0;
    sleepLogicReset();
#if defined(DEVICE_ROLE_SENSOR)
    s_sensorCriticalBelowCount = 0;
    s_sensorCriticalIntentActive = false;
    s_sensorCriticalSleepNow = false;
    s_sensorCriticalRetries = 0;
#endif
    return;
  }

#if defined(DEVICE_ROLE_SENSOR)
  if (s_sensorCriticalIntentActive) {
    if ((int32_t)(nowMs - s_sensorCriticalAckDeadlineMs) >= 0) {
      if (s_sensorCriticalRetries < MAX_RETRIES) {
        const bool sent = sendPendingCriticalIntent();
        s_sensorCriticalRetries++;
        s_sensorCriticalAckDeadlineMs = nowMs + retryBackoffMs(s_sensorCriticalRetries - 1);
        Serial.print("CRITICAL_SLEEP retry sent=");
        Serial.print(sent ? 1 : 0);
        Serial.print(" retry=");
        Serial.println((unsigned long)s_sensorCriticalRetries);
      } else {
        Serial.println("CRITICAL_SLEEP ack timeout -> forcing eternal sleep");
        s_sensorCriticalIntentActive = false;
        s_sensorCriticalSleepNow = true;
      }
    }
    return;
  }
#endif

  MsgSleepAck retryAck{};
  if (sleepLogicBuildRetryAck(nowMs, &retryAck)) {
    (void)espnowEnsurePeer(s_pendingHeadMac, ESPNOW_CHANNEL, false);
    const bool retrySent = espnowSend(s_pendingHeadMac, reinterpret_cast<const uint8_t*>(&retryAck), sizeof(retryAck));
    sleepLogicNotifyAckSent(retrySent, nowMs);
    Serial.print("SLEEP_ACK retry sent=");
    Serial.print(retrySent ? 1 : 0);
    Serial.print(" planId=");
    Serial.println((unsigned long)retryAck.planId);
  }

  if (s_waitingAck) {
    if ((int32_t)(nowMs - s_ackDeadlineMs) >= 0) {
      if (s_retryCount < MAX_RETRIES) {
        const uint32_t backoffMs = retryBackoffMs(s_retryCount);
        const bool sent = sendPendingTelemetry();
        s_retryCount++;
        s_ackDeadlineMs = nowMs + backoffMs;

        Serial.print("TELEMETRY retry sent=");
        Serial.print(sent ? 1 : 0);
        Serial.print(" seq=");
        Serial.print((unsigned long)s_lastSentSeq);
        Serial.print(" retry=");
        Serial.print((unsigned long)s_retryCount);
        Serial.print(" waitMs=");
        Serial.println((unsigned long)backoffMs);
      } else {
        Serial.print("TELEMETRY failed no-ack seq=");
        Serial.print((unsigned long)s_lastSentSeq);
        Serial.print(" retries=");
        Serial.println((unsigned long)s_retryCount);
        if (buttonIsDebugEnabled()) {
          ledsPulseOnce(120);
        }
        s_waitingAck = false;
        sleepLogicReset();
        if (s_noAckCycles < 255) {
          s_noAckCycles++;
        }
        if (s_noAckCycles >= MAX_NO_ACK_CYCLES_BEFORE_REJOIN) {
          Serial.println("PAIRING(NODE): no-ack threshold reached, keep paired and continue telemetry retries");
          s_noAckCycles = MAX_NO_ACK_CYCLES_BEFORE_REJOIN;
        }
      }
    }
    return;
  }

  if (!hasMeasurement || !measurement) {
    return;
  }

  if (s_nextTelemetryDueMs == 0) {
    s_nextTelemetryDueMs = nowMs +
                           TELEMETRY_FIRST_SEND_MIN_DELAY_MS +
                           deterministicNodePhaseOffsetMs() +
                           randomBoundedMs(TELEMETRY_FIRST_SEND_JITTER_MS + 1);
  }

  if ((int32_t)(nowMs - s_nextTelemetryDueMs) < 0) {
    return;
  }
  s_nextTelemetryDueMs = nowMs + nextTelemetryIntervalMs();

  uint8_t headMac[6] = {0};
  if (!pairingNodeHeadMac(headMac)) {
    return;
  }

  s_pendingTelemetry.hdr.ver = PROTO_VER;
  s_pendingTelemetry.hdr.type = MSG_TELEMETRY;
  s_pendingTelemetry.hdr.seq = ++s_telemetrySeq;
  s_pendingTelemetry.hdr.nodeId = pairingNodeId();

  s_pendingTelemetry.moisturePermille = measurement->moisturePermille;
  s_pendingTelemetry.moistureRawMv = measurement->moistureRawMv;
  s_pendingTelemetry.batteryRawMv = measurement->batteryRawMv;
  s_pendingTelemetry.batteryEstMv = measurement->batteryEstMv;
  s_pendingTelemetry.flags = static_cast<uint8_t>(FLAG_DIAG_RAW_PRESENT | FLAG_BATT_EST_VALID | s_nodeStatusFlags);
#if defined(DEVICE_ROLE_CONTROL)
  s_pendingTelemetry.flags |= FLAG_NODE_ROLE_CONTROL;
#endif
  s_pendingTelemetry.reserved = 0;

  memcpy(s_pendingHeadMac, headMac, 6);

#if defined(DEVICE_ROLE_SENSOR)
  const bool externalPowerReading = isExternalPowerReading(measurement->batteryEstMv);
#endif

  const bool sent = sendPendingTelemetry();
  if (buttonIsDebugEnabled()) {
    ledsPulseOnce(20);
  }
  s_lastSentSeq = s_pendingTelemetry.hdr.seq;
  s_waitingAck = true;
  s_retryCount = 0;
  s_ackDeadlineMs = nowMs + ACK_TIMEOUT_MS;
  s_lastSendStartMs = nowMs;

  if (!sent) {
    s_waitingAck = false;
    sleepLogicReset();
  } else {
    sleepLogicOnTelemetrySent(s_lastSentSeq, nowMs);
#if defined(DEVICE_ROLE_SENSOR)
    bool shouldStartCriticalIntent = false;
    if (externalPowerReading) {
      s_sensorCriticalBelowCount = 0;
    } else if (measurement->batteryEstMv < SENSOR_CRITICAL_SLEEP_MV) {
      if (s_sensorCriticalBelowCount < 255) {
        s_sensorCriticalBelowCount++;
      }
      shouldStartCriticalIntent = (s_sensorCriticalBelowCount >= SENSOR_CRITICAL_SLEEP_TELEMETRY_COUNT);
    } else {
      s_sensorCriticalBelowCount = 0;
    }

    if (shouldStartCriticalIntent) {
      beginSensorCriticalSleepIntent(measurement->batteryEstMv, nowMs);
    }
#endif
  }

  Serial.print("TELEMETRY sent=");
  Serial.print(sent ? 1 : 0);
  Serial.print(" seq=");
  Serial.print((unsigned long)s_pendingTelemetry.hdr.seq);
  Serial.print(" nodeId=");
  Serial.print((unsigned long)s_pendingTelemetry.hdr.nodeId);
  Serial.print(" moisturePermille=");
  Serial.print((unsigned long)s_pendingTelemetry.moisturePermille);
  Serial.print(" moistureRawMv=");
  Serial.print((unsigned long)s_pendingTelemetry.moistureRawMv);
  Serial.print(" batteryRawMv=");
  Serial.print((unsigned long)s_pendingTelemetry.batteryRawMv);
  Serial.print(" batteryEstMv=");
  Serial.print((unsigned long)s_pendingTelemetry.batteryEstMv);
  Serial.print(" flags=");
  Serial.println((unsigned long)s_pendingTelemetry.flags);
}

#elif defined(DEVICE_ROLE_HEAD)

#include "button.h"
#include "head_observability.h"
#include "leds.h"
#include "track_storage.h"

static const uint8_t MAX_NODE_REGISTRY = 8;
static const size_t TRACK_STORAGE_CAPACITY = 4096;
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint32_t EXPECTED_TELEMETRY_PERIOD_MS = TELEMETRY_BASE_INTERVAL_MS + TELEMETRY_INTERVAL_JITTER_MS;
static const uint32_t NODE_SUSPECT_TIMEOUT_MS = 6 * EXPECTED_TELEMETRY_PERIOD_MS;
static const uint32_t NODE_OFFLINE_TIMEOUT_MS = 18 * EXPECTED_TELEMETRY_PERIOD_MS;
static const uint32_t TELEMETRY_DUPLICATE_WINDOW_MS = 1500;

static const char* nodeStateToText(TelemetryHeadNodeState state)
{
  switch (state) {
    case TELEMETRY_HEAD_NODE_ONLINE:
      return "online";
    case TELEMETRY_HEAD_NODE_SUSPECT:
      return "suspect";
    case TELEMETRY_HEAD_NODE_OFFLINE:
      return "offline";
    default:
      return "unknown";
  }
}

struct NodeTelemetryState {
  bool used;
  bool isControl;
  bool lowBatteryLockout;
  bool irrigationActive;
  TelemetryHeadNodeState state;
  TelemetryHeadBatteryState batteryState;
  bool hasLastSeq;
  uint16_t nodeId;
  uint16_t moisturePermille;
  uint16_t batteryEstMv;
  uint8_t mac[6];
  uint32_t lastSeenMs;
  uint16_t lastSeq;
  uint32_t rxPackets;
  uint32_t rxDuplicates;
  uint32_t rxInvalid;
  uint32_t ackOkSent;
  uint32_t ackNotPairedSent;
  bool sleepAwaitAck;
  bool sleepAcked;
  uint8_t sleepRetries;
  uint16_t sleepPlanSeq;
  uint32_t sleepPlanId;
  uint32_t sleepHeadBootId;
  uint32_t sleepPlannedMs;
  uint32_t sleepValidUntilMs;
  uint32_t sleepNextRetryAtMs;
  uint32_t sleepExpectedReportDeadlineMs;
  uint8_t controlSleepTier;
  uint8_t controlBelow3500Count;
  uint8_t controlBelow3300Count;
  int32_t lastArrivalOffsetMs;
};

static NodeTelemetryState s_nodes[MAX_NODE_REGISTRY] = {};
static uint16_t s_ackSeq = 0;
static TelemetryHeadCommandAck s_latestCommandAck = {};
static uint32_t s_sleepPlanIdSeq = 1;
static uint32_t s_headBootId = 1;
static uint32_t s_waveReferenceTimeMs = 0;
static uint32_t s_defaultSleepBaseMs = SLEEP_BASE_DURATION_MS;
static uint32_t s_sleepBaseOverrideMs = 0;
static uint32_t s_sleepBaseOverrideUntilMs = 0;

static bool isSleepBaseOverrideActive(uint32_t nowMs)
{
  return s_sleepBaseOverrideMs != 0 &&
         s_sleepBaseOverrideUntilMs != 0 &&
         (int32_t)(nowMs - s_sleepBaseOverrideUntilMs) < 0;
}

static bool sendSleepPlanToNode(NodeTelemetryState* node, uint32_t nowMs, bool isRetry);

static uint8_t commandAckPriority(uint8_t status)
{
  if (status == COMMAND_ACK_STATUS_APPLIED) {
    return 3;
  }
  if (status == COMMAND_ACK_STATUS_REJECTED) {
    return 2;
  }
  if (status == COMMAND_ACK_STATUS_RECEIVED) {
    return 1;
  }
  return 0;
}

static bool shouldReplaceLatestCommandAck(const TelemetryHeadCommandAck& current, const MsgCommandAck& incoming)
{
  if (!current.valid) {
    return true;
  }

  const bool sameCommand =
      current.nodeId == incoming.hdr.nodeId &&
      current.cmdId == incoming.cmdId &&
      current.action == incoming.action;
  if (!sameCommand) {
    return true;
  }

  return commandAckPriority(incoming.status) >= commandAckPriority(current.status);
}

static TelemetryHeadBatteryState classifyBatteryState(uint16_t batteryEstMv)
{
  if (batteryEstMv <= BATTERY_NEEDS_REPLACEMENT_MV) {
    return TELEMETRY_HEAD_BATTERY_NEEDS_REPLACEMENT;
  }
  if (batteryEstMv <= BATTERY_CRITICAL_MV) {
    return TELEMETRY_HEAD_BATTERY_CRITICAL;
  }
  return TELEMETRY_HEAD_BATTERY_OK;
}

void telemetryInit()
{
  memset(s_nodes, 0, sizeof(s_nodes));
  s_ackSeq = 0;
  s_sleepPlanIdSeq = 1;
  s_defaultSleepBaseMs = SLEEP_BASE_DURATION_MS;
  s_sleepBaseOverrideMs = 0;
  s_sleepBaseOverrideUntilMs = 0;
  s_headBootId = static_cast<uint32_t>(esp_random());
  if (s_headBootId == 0) {
    s_headBootId = 1;
  }
  trackStorageInit(TRACK_STORAGE_CAPACITY);
}

void telemetryHeadSetSleepBaseOverrideMs(uint32_t baseSleepMs, uint32_t leaseMs)
{
  const uint32_t nowMs = millis();

  if (baseSleepMs == 0 || leaseMs == 0) {
    if (s_sleepBaseOverrideMs != 0 || s_sleepBaseOverrideUntilMs != 0) {
      s_sleepBaseOverrideMs = 0;
      s_sleepBaseOverrideUntilMs = 0;
      Serial.println("HEAD_KEEP_AWAKE override=off");
    }
    return;
  }

  if (baseSleepMs < 1000UL) {
    baseSleepMs = 1000UL;
  }
  if (baseSleepMs > s_defaultSleepBaseMs) {
    baseSleepMs = s_defaultSleepBaseMs;
  }

  const uint32_t newUntilMs = nowMs + leaseMs;
  const bool baseChanged = (s_sleepBaseOverrideMs != baseSleepMs);
  const bool wasExpired = (s_sleepBaseOverrideUntilMs != 0) && ((int32_t)(nowMs - s_sleepBaseOverrideUntilMs) >= 0);
  s_sleepBaseOverrideMs = baseSleepMs;
  s_sleepBaseOverrideUntilMs = newUntilMs;

  if (baseChanged || wasExpired) {
    Serial.print("HEAD_KEEP_AWAKE override=on baseMs=");
    Serial.print((unsigned long)s_sleepBaseOverrideMs);
    Serial.print(" leaseMs=");
    Serial.println((unsigned long)leaseMs);
  }
}

void telemetryHeadSetDefaultSleepBaseMs(uint32_t baseSleepMs)
{
  if (baseSleepMs < 1000UL) {
    baseSleepMs = 1000UL;
  }
  if (baseSleepMs > 86400000UL) {
    baseSleepMs = 86400000UL;
  }
  s_defaultSleepBaseMs = baseSleepMs;

  if (s_sleepBaseOverrideMs > s_defaultSleepBaseMs) {
    s_sleepBaseOverrideMs = s_defaultSleepBaseMs;
  }
}

void telemetryHeadResendSleepPlansToOnlineNodes()
{
  const uint32_t nowMs = millis();
  for (uint8_t i = 0; i < MAX_NODE_REGISTRY; ++i) {
    NodeTelemetryState* entry = &s_nodes[i];
    if (!entry->used || entry->state != TELEMETRY_HEAD_NODE_ONLINE) {
      continue;
    }
    (void)sendSleepPlanToNode(entry, nowMs, false);
  }
}

static uint8_t mapTrackFlags(const MsgTelemetry* telemetry, bool isDuplicate)
{
  uint8_t trackFlags = 0;
  if ((telemetry->flags & FLAG_NODE_ROLE_CONTROL) != 0) {
    trackFlags |= TRACK_FL_CONTROL;
  }
  if ((telemetry->flags & FLAG_NODE_LOW_BATTERY_LOCKOUT) != 0) {
    trackFlags |= TRACK_FL_LOW_BATTERY_LOCKOUT;
  }
  if ((telemetry->flags & FLAG_BATT_EST_VALID) != 0) {
    trackFlags |= TRACK_FL_BATT_EST_VALID;
  }
  if ((telemetry->flags & FLAG_CAL_VALID) != 0) {
    trackFlags |= TRACK_FL_CAL_VALID;
  }
  if ((telemetry->flags & FLAG_DIAG_RAW_PRESENT) != 0) {
    trackFlags |= TRACK_FL_RAW_PRESENT;
  }
  if ((telemetry->flags & FLAG_IRRIGATION_ACTIVE) != 0) {
    trackFlags |= TRACK_FL_IRRIGATION_ACTIVE;
  }
  if (isDuplicate) {
    trackFlags |= TRACK_FL_DUPLICATE;
  }
  return trackFlags;
}

static void pushTrackRecord(const MsgTelemetry* telemetry, const uint8_t src_mac[6], uint32_t nowMs, bool isDuplicate)
{
  TrackRecord rec{};
  rec.tsMs = nowMs;
  rec.nodeId = telemetry->hdr.nodeId;
  rec.telemetrySeq = telemetry->hdr.seq;
  rec.moisturePermille = telemetry->moisturePermille;
  rec.moistureRawMv = telemetry->moistureRawMv;
  rec.batteryRawMv = telemetry->batteryRawMv;
  rec.batteryEstMv = telemetry->batteryEstMv;
  rec.flags = mapTrackFlags(telemetry, isDuplicate);
  memcpy(rec.mac, src_mac, sizeof(rec.mac));
  (void)trackStoragePush(rec);
}

static NodeTelemetryState* getOrCreateNodeState(uint16_t nodeId, const uint8_t src_mac[6], uint32_t nowMs)
{
  NodeTelemetryState* emptySlot = nullptr;
  NodeTelemetryState* oldestSlot = &s_nodes[0];

  for (uint8_t i = 0; i < MAX_NODE_REGISTRY; ++i) {
    NodeTelemetryState* entry = &s_nodes[i];
    if (entry->used) {
      if (entry->nodeId == nodeId) {
        memcpy(entry->mac, src_mac, 6);
        return entry;
      }
      if (entry->lastSeenMs < oldestSlot->lastSeenMs) {
        oldestSlot = entry;
      }
    } else if (!emptySlot) {
      emptySlot = entry;
    }
  }

  NodeTelemetryState* target = emptySlot ? emptySlot : oldestSlot;
  target->used = true;
  target->state = TELEMETRY_HEAD_NODE_ONLINE;
  target->batteryState = TELEMETRY_HEAD_BATTERY_OK;
  target->hasLastSeq = false;
  target->nodeId = nodeId;
  target->moisturePermille = 0;
  target->batteryEstMv = 0;
  memcpy(target->mac, src_mac, 6);
  target->lastSeenMs = nowMs;
  target->lastSeq = 0;
  target->rxPackets = 0;
  target->rxDuplicates = 0;
  target->rxInvalid = 0;
  target->ackOkSent = 0;
  target->ackNotPairedSent = 0;
  target->isControl = false;
  target->lowBatteryLockout = false;
  target->irrigationActive = false;
  target->sleepAwaitAck = false;
  target->sleepAcked = false;
  target->sleepRetries = 0;
  target->sleepPlanSeq = 0;
  target->sleepPlanId = 0;
  target->sleepHeadBootId = 0;
  target->sleepPlannedMs = 0;
  target->sleepValidUntilMs = 0;
  target->sleepNextRetryAtMs = 0;
  target->sleepExpectedReportDeadlineMs = 0;
  target->controlSleepTier = 0;
  target->controlBelow3500Count = 0;
  target->controlBelow3300Count = 0;
  target->lastArrivalOffsetMs = 0;
  return target;
}

static NodeTelemetryState* findNodeState(uint16_t nodeId, const uint8_t src_mac[6])
{
  for (uint8_t i = 0; i < MAX_NODE_REGISTRY; ++i) {
    NodeTelemetryState* entry = &s_nodes[i];
    if (!entry->used) {
      continue;
    }
    if (entry->nodeId == nodeId && memcmp(entry->mac, src_mac, 6) == 0) {
      return entry;
    }
  }
  return nullptr;
}

static uint32_t headRandomBoundedMs(uint32_t maxExclusive)
{
  if (maxExclusive == 0) {
    return 0;
  }
  return static_cast<uint32_t>(esp_random() % maxExclusive);
}

static uint32_t computeNodeSlotDelayMs(uint16_t nodeId)
{
  if (SLEEP_SLOT_MAX_UNITS == 0 || nodeId == 0) {
    return 0;
  }
  const uint32_t slot = static_cast<uint32_t>((nodeId - 1) % SLEEP_SLOT_MAX_UNITS);
  const uint32_t deterministic = slot * SLEEP_SLOT_WIDTH_MS;
  const uint32_t microJitter = headRandomBoundedMs(SLEEP_SLOT_MICRO_JITTER_MS + 1);
  return deterministic + microJitter;
}

static uint32_t computeAlignedSleepMs(uint32_t nowMs,
                                      uint32_t referenceTimeMs,
                                      uint32_t baseSleepMs,
                                      uint32_t slotDelayMs)
{
  if (baseSleepMs == 0) {
    return slotDelayMs;
  }

  const int64_t phaseAnchorMs = static_cast<int64_t>(referenceTimeMs) + static_cast<int64_t>(slotDelayMs);
  int64_t targetWakeMs = phaseAnchorMs;
  if (targetWakeMs <= static_cast<int64_t>(nowMs)) {
    const int64_t elapsedMs = static_cast<int64_t>(nowMs) - phaseAnchorMs;
    const int64_t cyclesToAdvance = (elapsedMs / static_cast<int64_t>(baseSleepMs)) + 1;
    targetWakeMs += cyclesToAdvance * static_cast<int64_t>(baseSleepMs);
  }

  const int64_t sleepMs = targetWakeMs - static_cast<int64_t>(nowMs);
  if (sleepMs <= 0) {
    return baseSleepMs;
  }
  return static_cast<uint32_t>(sleepMs);
}

static uint32_t controlSleepBaseMs(const NodeTelemetryState* node, uint32_t nowMs)
{
  if (isSleepBaseOverrideActive(nowMs)) {
    return s_sleepBaseOverrideMs;
  }

  if (!node || !node->isControl) {
    return s_defaultSleepBaseMs;
  }

  if (node->batteryEstMv <= BATTERY_EXTERNAL_POWER_MAX_MV) {
    return s_defaultSleepBaseMs;
  }

  if (node->controlSleepTier >= 2) {
    return CONTROL_SLEEP_TIER2_DURATION_MS;
  }
  if (node->controlSleepTier == 1) {
    return CONTROL_SLEEP_TIER1_DURATION_MS;
  }
  return s_defaultSleepBaseMs;
}

static void updateControlSleepTier(NodeTelemetryState* node)
{
  if (!node || !node->isControl) {
    return;
  }

  const uint16_t battMv = node->batteryEstMv;
  if (battMv <= BATTERY_EXTERNAL_POWER_MAX_MV) {
    node->controlSleepTier = 0;
    node->controlBelow3500Count = 0;
    node->controlBelow3300Count = 0;
    return;
  }

  if (battMv <= CONTROL_SLEEP_TIER1_THRESHOLD_MV) {
    if (node->controlBelow3500Count < 255) {
      node->controlBelow3500Count++;
    }
  } else {
    node->controlBelow3500Count = 0;
  }

  if (battMv <= CONTROL_SLEEP_TIER2_THRESHOLD_MV) {
    if (node->controlBelow3300Count < 255) {
      node->controlBelow3300Count++;
    }
  } else {
    node->controlBelow3300Count = 0;
  }

  if (node->controlSleepTier >= 2) {
    if (battMv >= static_cast<uint16_t>(CONTROL_SLEEP_TIER2_THRESHOLD_MV + BATTERY_HYSTERESIS_MV)) {
      node->controlSleepTier = 1;
    }
  } else if (node->controlSleepTier == 1) {
    if (battMv >= static_cast<uint16_t>(CONTROL_SLEEP_TIER1_THRESHOLD_MV + BATTERY_HYSTERESIS_MV)) {
      node->controlSleepTier = 0;
    } else if (node->controlBelow3300Count >= CONTROL_SLEEP_TRANSITION_COUNT) {
      node->controlSleepTier = 2;
    }
  } else {
    if (node->controlBelow3500Count >= CONTROL_SLEEP_TRANSITION_COUNT) {
      node->controlSleepTier = 1;
    }
  }
}

static bool sendCriticalSleepAckToNode(uint16_t nodeId, const uint8_t src_mac[6], uint16_t ackSeq, uint8_t status)
{
  MsgCriticalSleepAck ack{};
  ack.hdr.ver = PROTO_VER;
  ack.hdr.type = MSG_CRITICAL_SLEEP_ACK;
  ack.hdr.seq = ++s_ackSeq;
  ack.hdr.nodeId = nodeId;
  ack.ackSeq = ackSeq;
  ack.status = status;
  ack.reserved = 0;

  (void)espnowEnsurePeer(src_mac, ESPNOW_CHANNEL, false);
  return espnowSend(src_mac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
}

static bool sendSleepPlanToNode(NodeTelemetryState* node, uint32_t nowMs, bool isRetry)
{
  if (!node || !node->used || node->state != TELEMETRY_HEAD_NODE_ONLINE) {
    return false;
  }

  if (!isRetry) {
    const bool hadActiveSleepWindow =
        node->sleepAcked &&
        node->sleepExpectedReportDeadlineMs != 0 &&
        (int32_t)(node->sleepExpectedReportDeadlineMs - nowMs) > 0;
    const uint32_t prevExpectedDeadlineMs = node->sleepExpectedReportDeadlineMs;

    node->sleepPlanId = s_sleepPlanIdSeq++;
    if (s_sleepPlanIdSeq == 0) {
      s_sleepPlanIdSeq = 1;
    }
    node->sleepHeadBootId = s_headBootId;
    const uint32_t slotDelayMs = computeNodeSlotDelayMs(node->nodeId);
    const uint32_t baseSleepMs = controlSleepBaseMs(node, nowMs);
    const bool keepAwakeOverrideActive = isSleepBaseOverrideActive(nowMs);

    uint32_t plannedMs = baseSleepMs + slotDelayMs;
    if (s_waveReferenceTimeMs > 0 && !keepAwakeOverrideActive) {
      plannedMs = computeAlignedSleepMs(nowMs, s_waveReferenceTimeMs, baseSleepMs, slotDelayMs);
    }

    uint32_t minSleepClampMs = 3000;
    if (baseSleepMs < minSleepClampMs) {
      minSleepClampMs = baseSleepMs;
    }
    const uint32_t maxSleepClampMs = baseSleepMs + TELEMETRY_PHASE_SPREAD_MS + SLEEP_SLOT_WIDTH_MS + SLEEP_SLOT_MICRO_JITTER_MS;
    if (plannedMs < minSleepClampMs) {
      plannedMs = minSleepClampMs;
    }
    if (plannedMs > maxSleepClampMs) {
      plannedMs = maxSleepClampMs;
    }

    node->sleepPlannedMs = plannedMs;
    node->sleepValidUntilMs = nowMs + SLEEP_PLAN_VALID_WINDOW_MS;
    node->sleepAwaitAck = true;
    // If node is currently in an acknowledged sleep window, keep that state
    // until current wake to avoid false "lost" transitions when interval shrinks.
    node->sleepAcked = hadActiveSleepWindow;
    node->sleepRetries = 0;
    const uint32_t plannedDeadlineMs = nowMs + node->sleepPlannedMs + SLEEP_EXPECTED_WAKE_GRACE_MS;
    node->sleepExpectedReportDeadlineMs = hadActiveSleepWindow
      ? ((plannedDeadlineMs > prevExpectedDeadlineMs) ? plannedDeadlineMs : prevExpectedDeadlineMs)
      : plannedDeadlineMs;
  }

  MsgSleepPlan plan{};
  plan.hdr.ver = PROTO_VER;
  plan.hdr.type = MSG_SLEEP_PLAN;
  plan.hdr.seq = ++s_ackSeq;
  plan.hdr.nodeId = node->nodeId;
  plan.planId = node->sleepPlanId;
  plan.headBootId = node->sleepHeadBootId;
  plan.sleepMs = node->sleepPlannedMs;
  plan.validUntilMs = node->sleepValidUntilMs;
  const uint32_t baseSleepMs = controlSleepBaseMs(node, nowMs);
  plan.baseSleepSec = static_cast<uint16_t>(baseSleepMs / 1000UL);
  plan.slotDelayMs = static_cast<uint16_t>(node->sleepPlannedMs - baseSleepMs);

  node->sleepPlanSeq = plan.hdr.seq;
  node->sleepNextRetryAtMs = nowMs + SLEEP_ACK_RETRY_MIN_MS + headRandomBoundedMs(SLEEP_ACK_RETRY_JITTER_MS + 1);

  // Log sleep plan with phase sync info
  Serial.print("[nodeId=");
  Serial.print((unsigned long)node->nodeId);
  Serial.print("] sleep_plan sleepMs=");
  Serial.print((unsigned long)plan.sleepMs);
  Serial.print(" baseMs=");
  Serial.print((unsigned long)baseSleepMs);
  Serial.print(" slotDelayMs=");
  Serial.print((unsigned long)plan.slotDelayMs);
  Serial.print(" refMs=");
  Serial.print((unsigned long)s_waveReferenceTimeMs);
  Serial.print(" arrivalOffsetMs=");
  Serial.print((long)node->lastArrivalOffsetMs);
  Serial.println("");

  (void)espnowEnsurePeer(node->mac, ESPNOW_CHANNEL, false);
  const bool sent = espnowSend(node->mac, reinterpret_cast<const uint8_t*>(&plan), sizeof(plan));

  Serial.print("[nodeId=");
  Serial.print((unsigned long)node->nodeId);
  Serial.print("] sleep_plan sent=");
  Serial.print(sent ? 1 : 0);
  Serial.print(" retry=");
  Serial.print(isRetry ? 1 : 0);
  Serial.print(" planId=");
  Serial.print((unsigned long)plan.planId);
  Serial.print(" sleepMs=");
  Serial.println((unsigned long)plan.sleepMs);
  return sent;
}

static bool sendSleepAckAckToNode(NodeTelemetryState* node)
{
  if (!node || !node->used) {
    return false;
  }

  MsgSleepAckAck msg{};
  msg.hdr.ver = PROTO_VER;
  msg.hdr.type = MSG_SLEEP_ACK_ACK;
  msg.hdr.seq = ++s_ackSeq;
  msg.hdr.nodeId = node->nodeId;
  msg.planId = node->sleepPlanId;
  msg.headBootId = node->sleepHeadBootId;
  msg.commit = 1;

  (void)espnowEnsurePeer(node->mac, ESPNOW_CHANNEL, false);
  const bool sent = espnowSend(node->mac, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));

  Serial.print("[nodeId=");
  Serial.print((unsigned long)node->nodeId);
  Serial.print("] sleep_ack_ack sent=");
  Serial.print(sent ? 1 : 0);
  Serial.print(" planId=");
  Serial.println((unsigned long)msg.planId);
  return sent;
}

static void logTelemetry(const MsgTelemetry* telemetry, const uint8_t src_mac[6])
{
  char macBuf[18] = {0};
  macToString(src_mac, macBuf, sizeof(macBuf));

  Serial.print("[nodeId=");
  Serial.print((unsigned long)telemetry->hdr.nodeId);
  Serial.print(" mac=");
  Serial.print(macBuf);
  Serial.print("] telemetry seq=");
  Serial.print((unsigned long)telemetry->hdr.seq);
  Serial.print(" moisturePermille=");
  Serial.print((unsigned long)telemetry->moisturePermille);
  Serial.print(" moistureRawMv=");
  Serial.print((unsigned long)telemetry->moistureRawMv);
  Serial.print(" batteryRawMv=");
  Serial.print((unsigned long)telemetry->batteryRawMv);
  Serial.print(" batteryEstMv=");
  Serial.print((unsigned long)telemetry->batteryEstMv);
  const bool rawPresent = (telemetry->flags & FLAG_DIAG_RAW_PRESENT) != 0;
  const bool calValid = (telemetry->flags & FLAG_CAL_VALID) != 0;
  const bool battEstValid = (telemetry->flags & FLAG_BATT_EST_VALID) != 0;

  Serial.print(" flags=0x");
  if (telemetry->flags < 0x10) {
    Serial.print('0');
  }
  Serial.print((unsigned long)telemetry->flags, HEX);
  Serial.print(" rawPresent=");
  Serial.print(rawPresent ? 1 : 0);
  Serial.print(" battEstValid=");
  Serial.print(battEstValid ? 1 : 0);
  Serial.print(" calValid=");
  Serial.println(calValid ? 1 : 0);
}

static void sendTelemetryAck(const uint8_t src_mac[6], uint16_t nodeId, uint16_t telemetrySeq, uint8_t status)
{
  MsgTelemetryAck ack{};
  ack.hdr.ver = PROTO_VER;
  ack.hdr.type = MSG_TELEMETRY_ACK;
  ack.hdr.seq = ++s_ackSeq;
  ack.hdr.nodeId = nodeId;
  ack.ackSeq = telemetrySeq;
  ack.status = status;
  ack.reserved = 0;

  (void)espnowEnsurePeer(src_mac, ESPNOW_CHANNEL, false);
  const bool sent = espnowSend(src_mac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));

  NodeTelemetryState* nodeState = findNodeState(nodeId, src_mac);
  if (nodeState) {
    if (status == TELEMETRY_ACK_STATUS_OK) {
      nodeState->ackOkSent++;
    } else if (status == TELEMETRY_ACK_STATUS_NOT_PAIRED) {
      nodeState->ackNotPairedSent++;
    }
  }

  char macBuf[18] = {0};
  macToString(src_mac, macBuf, sizeof(macBuf));
  Serial.print("[nodeId=");
  Serial.print((unsigned long)nodeId);
  Serial.print(" mac=");
  Serial.print(macBuf);
  Serial.print("] telemetry_ack sent=");
  Serial.print(sent ? 1 : 0);
  Serial.print(" status=");
  Serial.print((unsigned long)status);
  Serial.print(" ackSeq=");
  Serial.println((unsigned long)telemetrySeq);
}

void telemetryOnRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (!src_mac || !data) {
    return;
  }

  if (len >= (int)sizeof(MsgHdr)) {
    const MsgHdr* hdr = reinterpret_cast<const MsgHdr*>(data);
    if (hdr->ver == PROTO_VER && hdr->type == MSG_CRITICAL_SLEEP_INTENT && len == (int)sizeof(MsgCriticalSleepIntent)) {
      const MsgCriticalSleepIntent* intent = reinterpret_cast<const MsgCriticalSleepIntent*>(data);
      if (pairingHeadIsKnownNode(intent->hdr.nodeId, src_mac)) {
        const bool ackSent = sendCriticalSleepAckToNode(intent->hdr.nodeId, src_mac, intent->hdr.seq, CRITICAL_SLEEP_ACK_STATUS_OK);
        Serial.print("[nodeId=");
        Serial.print((unsigned long)intent->hdr.nodeId);
        Serial.print("] critical_sleep intent reason=");
        Serial.print((unsigned long)intent->reason);
        Serial.print(" battMv=");
        Serial.print((unsigned long)intent->batteryEstMv);
        Serial.print(" thresholdMv=");
        Serial.print((unsigned long)intent->thresholdMv);
        Serial.print(" ackSent=");
        Serial.println(ackSent ? 1 : 0);
      }
      return;
    }

    if (hdr->ver == PROTO_VER && hdr->type == MSG_SLEEP_ACK && len == (int)sizeof(MsgSleepAck)) {
      const MsgSleepAck* ack = reinterpret_cast<const MsgSleepAck*>(data);
      NodeTelemetryState* node = findNodeState(ack->hdr.nodeId, src_mac);
      if (node && pairingHeadIsKnownNode(ack->hdr.nodeId, src_mac)) {
        const bool planMatch = node->sleepAwaitAck &&
                               ack->planId == node->sleepPlanId &&
                               ack->headBootId == node->sleepHeadBootId;

        if (planMatch && ack->accepted) {
          node->sleepAwaitAck = false;
          node->sleepAcked = true;
          node->sleepExpectedReportDeadlineMs = millis() + node->sleepPlannedMs + SLEEP_EXPECTED_WAKE_GRACE_MS;
          (void)sendSleepAckAckToNode(node);
        } else if (planMatch && !ack->accepted) {
          node->sleepAwaitAck = false;
          node->sleepAcked = false;
          node->state = TELEMETRY_HEAD_NODE_SUSPECT;
        }

        Serial.print("[nodeId=");
        Serial.print((unsigned long)ack->hdr.nodeId);
        Serial.print("] sleep_ack accepted=");
        Serial.print((unsigned long)ack->accepted);
        Serial.print(" reject=");
        Serial.print((unsigned long)ack->rejectReason);
        Serial.print(" planId=");
        Serial.println((unsigned long)ack->planId);
      }
      return;
    }

    if (hdr->ver == PROTO_VER && hdr->type == MSG_COMMAND_ACK && len == (int)sizeof(MsgCommandAck)) {
      const MsgCommandAck* ack = reinterpret_cast<const MsgCommandAck*>(data);
      if (pairingHeadIsKnownNode(ack->hdr.nodeId, src_mac)) {
        if (shouldReplaceLatestCommandAck(s_latestCommandAck, *ack)) {
          s_latestCommandAck.valid = true;
          s_latestCommandAck.nodeId = ack->hdr.nodeId;
          s_latestCommandAck.cmdId = ack->cmdId;
          s_latestCommandAck.action = ack->action;
          s_latestCommandAck.status = ack->status;
          s_latestCommandAck.irrigationState = ack->irrigationState;
          s_latestCommandAck.remainingSec = ack->remainingSec;
          s_latestCommandAck.receivedAtMs = millis();
        }

        Serial.print("[nodeId=");
        Serial.print((unsigned long)ack->hdr.nodeId);
        Serial.print("] cmd_ack cmdId=");
        Serial.print((unsigned long)ack->cmdId);
        Serial.print(" action=");
        Serial.print((unsigned long)ack->action);
        Serial.print(" status=");
        Serial.print((unsigned long)ack->status);
        Serial.print(" irrigationState=");
        Serial.print((unsigned long)ack->irrigationState);
        Serial.print(" remainingSec=");
        Serial.println((unsigned long)ack->remainingSec);
      }
      return;
    }

    if (hdr->ver == PROTO_VER && hdr->type == MSG_REMOTE_BUTTON && len == (int)sizeof(MsgRemoteButton)) {
      const MsgRemoteButton* cmd = reinterpret_cast<const MsgRemoteButton*>(data);
      if (cmd->action == REMOTE_BUTTON_IRRIGATION_STATE_REQUEST &&
          pairingHeadIsKnownNode(cmd->hdr.nodeId, src_mac)) {
        const bool sent = headObservabilityRequestIrrigationSync();
        Serial.print("[nodeId=");
        Serial.print((unsigned long)cmd->hdr.nodeId);
        Serial.print("] irrigation_state_request handled sent=");
        Serial.println(sent ? 1 : 0);
      }
      return;
    }
  }

  if (len != (int)sizeof(MsgTelemetry)) {
    return;
  }

  const MsgTelemetry* telemetry = reinterpret_cast<const MsgTelemetry*>(data);
  if (telemetry->hdr.ver != PROTO_VER || telemetry->hdr.type != MSG_TELEMETRY) {
    return;
  }

  const uint32_t nowMs = millis();
  const bool fromCurrentPair = pairingHeadIsKnownNode(telemetry->hdr.nodeId, src_mac);
  NodeTelemetryState* nodeState = nullptr;
  if (fromCurrentPair) {
    nodeState = getOrCreateNodeState(telemetry->hdr.nodeId, src_mac, nowMs);
    if (!nodeState) {
      return;
    }
    nodeState->isControl = (telemetry->flags & FLAG_NODE_ROLE_CONTROL) != 0;
    nodeState->lowBatteryLockout = (telemetry->flags & FLAG_NODE_LOW_BATTERY_LOCKOUT) != 0;
    nodeState->irrigationActive = (telemetry->flags & FLAG_IRRIGATION_ACTIVE) != 0;
  } else {
    nodeState = findNodeState(telemetry->hdr.nodeId, src_mac);
  }

  if (!fromCurrentPair) {
    if (nodeState) {
      nodeState->rxInvalid++;
    }
    sendTelemetryAck(src_mac, telemetry->hdr.nodeId, telemetry->hdr.seq, TELEMETRY_ACK_STATUS_NOT_PAIRED);
    return;
  }

  if (nodeState->state != TELEMETRY_HEAD_NODE_ONLINE) {
    nodeState->state = TELEMETRY_HEAD_NODE_ONLINE;
    char macBuf[18] = {0};
    macToString(src_mac, macBuf, sizeof(macBuf));
    Serial.print("[nodeId=");
    Serial.print((unsigned long)nodeState->nodeId);
    Serial.print(" mac=");
    Serial.print(macBuf);
    Serial.println("] online");
  }

  const uint32_t prevSeenMs = nodeState->lastSeenMs;
  nodeState->lastSeenMs = nowMs;
  // If the node was in confirmed sleep (deep sleep = full reboot), the seq counter
  // resets on the node side.  Clear hasLastSeq so the first packet after wake is
  // never misclassified as a duplicate.
  const bool wasDeepSleepWake = nodeState->sleepAcked;
  nodeState->sleepAwaitAck = false;
  nodeState->sleepAcked = false;
  if (wasDeepSleepWake) {
    nodeState->hasLastSeq = false;
  }

  const bool sameSeq = nodeState->hasLastSeq && (nodeState->lastSeq == telemetry->hdr.seq);
  const bool payloadChanged =
      nodeState->moisturePermille != telemetry->moisturePermille ||
      nodeState->batteryEstMv != telemetry->batteryEstMv ||
      nodeState->isControl != ((telemetry->flags & FLAG_NODE_ROLE_CONTROL) != 0) ||
      nodeState->lowBatteryLockout != ((telemetry->flags & FLAG_NODE_LOW_BATTERY_LOCKOUT) != 0) ||
      nodeState->irrigationActive != ((telemetry->flags & FLAG_IRRIGATION_ACTIVE) != 0);
  const uint32_t interArrivalMs = nowMs - prevSeenMs;
  const bool isDuplicate = sameSeq && !payloadChanged && interArrivalMs <= TELEMETRY_DUPLICATE_WINDOW_MS;
  pushTrackRecord(telemetry, src_mac, nowMs, isDuplicate);

  if (isDuplicate) {
    nodeState->rxDuplicates++;
  } else {
    nodeState->hasLastSeq = true;
    nodeState->lastSeq = telemetry->hdr.seq;
    nodeState->isControl = (telemetry->flags & FLAG_NODE_ROLE_CONTROL) != 0;
    nodeState->lowBatteryLockout = (telemetry->flags & FLAG_NODE_LOW_BATTERY_LOCKOUT) != 0;
    nodeState->irrigationActive = (telemetry->flags & FLAG_IRRIGATION_ACTIVE) != 0;
    nodeState->moisturePermille = telemetry->moisturePermille;
    nodeState->batteryEstMv = telemetry->batteryEstMv;
    nodeState->batteryState = classifyBatteryState(telemetry->batteryEstMv);
    updateControlSleepTier(nodeState);
    nodeState->rxPackets++;
    logTelemetry(telemetry, src_mac);
    
    // Phase synchronization: establish wave reference on first telemetry
    if (s_waveReferenceTimeMs == 0) {
      s_waveReferenceTimeMs = nowMs;
      nodeState->lastArrivalOffsetMs = 0;
      Serial.print("[nodeId=");
      Serial.print((unsigned long)nodeState->nodeId);
      Serial.println("] phase_sync: REFERENCE SET (offset=0)");
    } else {
      nodeState->lastArrivalOffsetMs = static_cast<int32_t>(nowMs - s_waveReferenceTimeMs);
      Serial.print("[nodeId=");
      Serial.print((unsigned long)nodeState->nodeId);
      Serial.print("] phase_sync: offset=");
      Serial.print((long)nodeState->lastArrivalOffsetMs);
      Serial.println(" ms from reference");
    }
  }

  if (buttonIsDebugEnabled()) {
    ledsPulseOnce(120);
  }

  sendTelemetryAck(src_mac, telemetry->hdr.nodeId, telemetry->hdr.seq, TELEMETRY_ACK_STATUS_OK);

  if (!(nodeState->isControl && nodeState->irrigationActive)) {
    (void)sendSleepPlanToNode(nodeState, nowMs, false);
  }
}

void telemetryTickHead(uint32_t nowMs)
{
  if (s_sleepBaseOverrideMs != 0 && s_sleepBaseOverrideUntilMs != 0 &&
      (int32_t)(nowMs - s_sleepBaseOverrideUntilMs) >= 0) {
    s_sleepBaseOverrideMs = 0;
    s_sleepBaseOverrideUntilMs = 0;
    Serial.println("HEAD_KEEP_AWAKE override=expired");
  }

  for (uint8_t i = 0; i < MAX_NODE_REGISTRY; ++i) {
    NodeTelemetryState* entry = &s_nodes[i];
    if (!entry->used) {
      continue;
    }

    if (entry->sleepAwaitAck && (int32_t)(nowMs - entry->sleepNextRetryAtMs) >= 0) {
      if (entry->sleepRetries < SLEEP_ACK_RETRY_MAX) {
        const bool sent = sendSleepPlanToNode(entry, nowMs, true);
        if (sent) {
          entry->sleepRetries++;
        }
      } else {
        entry->sleepAwaitAck = false;
        entry->sleepAcked = false;
        entry->state = TELEMETRY_HEAD_NODE_SUSPECT;
      }
    }

    const uint32_t sinceLastMs = static_cast<uint32_t>(nowMs - entry->lastSeenMs);

    TelemetryHeadNodeState nextState = TELEMETRY_HEAD_NODE_ONLINE;
    if (entry->sleepAcked && entry->sleepExpectedReportDeadlineMs != 0) {
      const int32_t untilExpectedWakeMs = static_cast<int32_t>(entry->sleepExpectedReportDeadlineMs - nowMs);
      if (untilExpectedWakeMs > 0) {
        nextState = TELEMETRY_HEAD_NODE_ONLINE;
      } else {
        const uint32_t overdueMs = static_cast<uint32_t>(nowMs - entry->sleepExpectedReportDeadlineMs);
        if (overdueMs >= NODE_SUSPECT_TIMEOUT_MS) {
          nextState = TELEMETRY_HEAD_NODE_OFFLINE;
        } else {
          nextState = TELEMETRY_HEAD_NODE_SUSPECT;
        }
      }
    } else {
      if (sinceLastMs >= NODE_OFFLINE_TIMEOUT_MS) {
        nextState = TELEMETRY_HEAD_NODE_OFFLINE;
      } else if (sinceLastMs >= NODE_SUSPECT_TIMEOUT_MS) {
        nextState = TELEMETRY_HEAD_NODE_SUSPECT;
      }
    }

    if (nextState != entry->state) {
      entry->state = nextState;
      char macBuf[18] = {0};
      macToString(entry->mac, macBuf, sizeof(macBuf));
      Serial.print("[nodeId=");
      Serial.print((unsigned long)entry->nodeId);
      Serial.print(" mac=");
      Serial.print(macBuf);
      Serial.print("] ");
      Serial.println(nodeStateToText(entry->state));
    }
  }
}

uint8_t telemetryHeadGetPresence(TelemetryHeadNodePresence* outNodes, uint8_t maxNodes)
{
  if (!outNodes || maxNodes == 0) {
    return 0;
  }

  uint8_t written = 0;
  for (uint8_t i = 0; i < MAX_NODE_REGISTRY && written < maxNodes; ++i) {
    const NodeTelemetryState* entry = &s_nodes[i];
    if (!entry->used) {
      continue;
    }

    outNodes[written].used = true;
    outNodes[written].state = entry->state;
    outNodes[written].batteryState = entry->batteryState;
    outNodes[written].lowBatteryLockout = entry->lowBatteryLockout;
    outNodes[written].sleepAcked = entry->sleepAcked;
    outNodes[written].nodeId = entry->nodeId;
    outNodes[written].moisturePermille = entry->moisturePermille;
    outNodes[written].batteryEstMv = entry->batteryEstMv;
    memcpy(outNodes[written].mac, entry->mac, 6);
    outNodes[written].lastSeenMs = entry->lastSeenMs;
    outNodes[written].rxPackets = entry->rxPackets;
    outNodes[written].rxDuplicates = entry->rxDuplicates;
    outNodes[written].rxInvalid = entry->rxInvalid;
    outNodes[written].ackOkSent = entry->ackOkSent;
    outNodes[written].ackNotPairedSent = entry->ackNotPairedSent;
    outNodes[written].isControl = entry->isControl;
    outNodes[written].irrigationActive = entry->irrigationActive;
    outNodes[written].sleepExpectedReportDeadlineMs = entry->sleepExpectedReportDeadlineMs;
    written++;
  }
  return written;
}

void telemetryHeadClearPresence()
{
  memset(s_nodes, 0, sizeof(s_nodes));
}

bool telemetryHeadRemovePresenceByNodeId(uint16_t nodeId)
{
  if (nodeId == 0) {
    return false;
  }

  for (uint8_t i = 0; i < MAX_NODE_REGISTRY; ++i) {
    NodeTelemetryState* entry = &s_nodes[i];
    if (!entry->used) {
      continue;
    }
    if (entry->nodeId != nodeId) {
      continue;
    }
    memset(entry, 0, sizeof(*entry));
    return true;
  }
  return false;
}

bool telemetryHeadSendRemoteButtonAction(uint16_t nodeId, uint8_t action, uint16_t cmdId)
{
  const bool isCalibrationAction =
      action == REMOTE_BUTTON_CALIBRATE_START || action == REMOTE_BUTTON_CALIBRATE_MEASURE_WET;
  const bool isIrrigationAction =
    action == REMOTE_BUTTON_IRRIGATION_START || action == REMOTE_BUTTON_IRRIGATION_STOP ||
    action == REMOTE_BUTTON_IRRIGATION_STATE_REQUEST;

  if (!isCalibrationAction && !isIrrigationAction) {
    return false;
  }

  if (nodeId == 0) {
    if (!isIrrigationAction) {
      return false;
    }

    MsgRemoteButton command{};
    command.hdr.ver = PROTO_VER;
    command.hdr.type = MSG_REMOTE_BUTTON;
    command.hdr.seq = ++s_ackSeq;
    command.hdr.nodeId = 0;
    command.action = action;
    command.cmdId = cmdId;

    const bool sent = espnowSend(BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&command), sizeof(command));
    Serial.print("[nodeId=*] remote_btn broadcast sent=");
    Serial.print(sent ? 1 : 0);
    Serial.print(" action=");
    Serial.println((unsigned long)action);
    return sent;
  }

  NodeTelemetryState* node = nullptr;
  for (uint8_t i = 0; i < MAX_NODE_REGISTRY; ++i) {
    NodeTelemetryState* entry = &s_nodes[i];
    if (!entry->used || entry->nodeId != nodeId) {
      continue;
    }
    node = entry;
    break;
  }

  if (!node) {
    return false;
  }

  if (node->state != TELEMETRY_HEAD_NODE_ONLINE) {
    return false;
  }

  MsgRemoteButton command{};
  command.hdr.ver = PROTO_VER;
  command.hdr.type = MSG_REMOTE_BUTTON;
  command.hdr.seq = ++s_ackSeq;
  command.hdr.nodeId = nodeId;
  command.action = action;
  command.cmdId = cmdId;

  (void)espnowEnsurePeer(node->mac, ESPNOW_CHANNEL, false);
  const bool sent = espnowSend(node->mac, reinterpret_cast<const uint8_t*>(&command), sizeof(command));

  char macBuf[18] = {0};
  macToString(node->mac, macBuf, sizeof(macBuf));
  Serial.print("[nodeId=");
  Serial.print((unsigned long)nodeId);
  Serial.print(" mac=");
  Serial.print(macBuf);
  Serial.print("] remote_btn sent=");
  Serial.print(sent ? 1 : 0);
  Serial.print(" action=");
  Serial.println((unsigned long)action);

  return sent;
}

bool telemetryHeadConsumeCommandAck(TelemetryHeadCommandAck* outAck)
{
  if (!outAck || !s_latestCommandAck.valid) {
    return false;
  }
  *outAck = s_latestCommandAck;
  s_latestCommandAck.valid = false;
  return true;
}

bool telemetryHeadSendIrrigationState(uint8_t desiredState, uint64_t leaseId, uint32_t remainingLeaseMs)
{
  if (desiredState != IRRIGATION_STATE_OFF && desiredState != IRRIGATION_STATE_RUN) {
    return false;
  }

  MsgIrrigationState state{};
  state.hdr.ver = PROTO_VER;
  state.hdr.type = MSG_IRRIGATION_STATE;
  state.hdr.seq = ++s_ackSeq;
  state.hdr.nodeId = 0;
  state.desiredState = desiredState;
  state.reserved = 0;
  state.leaseId = leaseId;
  state.remainingLeaseMs = remainingLeaseMs;

  const bool sent = espnowSend(BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&state), sizeof(state));
  Serial.print("[nodeId=*] irrigation_state sent=");
  Serial.print(sent ? 1 : 0);
  Serial.print(" desired=");
  Serial.print((unsigned long)desiredState);
  Serial.print(" leaseId=");
  Serial.print((unsigned long long)leaseId);
  Serial.print(" remainingMs=");
  Serial.println((unsigned long)remainingLeaseMs);
  return sent;
}

void telemetrySetNodeStatusFlags(uint8_t mask, bool enabled)
{
  (void)mask;
  (void)enabled;
}

void telemetryTickSensor(const SensorMeasurement* measurement, bool hasMeasurement, uint32_t nowMs)
{
  (void)measurement;
  (void)hasMeasurement;
  (void)nowMs;
}

#else

void telemetryInit() {}

void telemetryOnRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  (void)src_mac;
  (void)data;
  (void)len;
}

void telemetryTickSensor(const SensorMeasurement* measurement, bool hasMeasurement, uint32_t nowMs)
{
  (void)measurement;
  (void)hasMeasurement;
  (void)nowMs;
}

void telemetryTickHead(uint32_t nowMs)
{
  (void)nowMs;
}

uint8_t telemetryHeadGetPresence(TelemetryHeadNodePresence* outNodes, uint8_t maxNodes)
{
  (void)outNodes;
  (void)maxNodes;
  return 0;
}

void telemetryHeadClearPresence() {}

bool telemetryHeadRemovePresenceByNodeId(uint16_t nodeId)
{
  (void)nodeId;
  return false;
}

bool telemetryHeadSendRemoteButtonAction(uint16_t nodeId, uint8_t action, uint16_t cmdId)
{
  (void)nodeId;
  (void)action;
  (void)cmdId;
  return false;
}

bool telemetryHeadSendIrrigationState(uint8_t desiredState, uint64_t leaseId, uint32_t remainingLeaseMs)
{
  (void)desiredState;
  (void)leaseId;
  (void)remainingLeaseMs;
  return false;
}

bool telemetryHeadConsumeCommandAck(TelemetryHeadCommandAck* outAck)
{
  (void)outAck;
  return false;
}

void telemetrySetNodeStatusFlags(uint8_t mask, bool enabled)
{
  (void)mask;
  (void)enabled;
}

void telemetryHeadSetSleepBaseOverrideMs(uint32_t baseSleepMs, uint32_t leaseMs)
{
  (void)baseSleepMs;
  (void)leaseMs;
}

void telemetryHeadSetDefaultSleepBaseMs(uint32_t baseSleepMs)
{
  (void)baseSleepMs;
}

void telemetryHeadResendSleepPlansToOnlineNodes() {}

#endif
