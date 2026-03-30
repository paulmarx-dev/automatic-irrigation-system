#include "head_observability.h"

#if defined(DEVICE_ROLE_HEAD)

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ctype.h>
#include <stdlib.h>

#include "esp_now_helpers.h"
#include "head_wifi_provisioning.h"
#include "pairing.h"
#include "protocol.h"
#include "telemetry.h"
#include "track_storage.h"

namespace {

static WebServer s_server(80);
static constexpr uint8_t MAX_SENSOR_LABELS = 8;
static constexpr size_t SENSOR_NAME_MAX = 32;
static constexpr uint16_t SENSOR_LABELS_NVS_VERSION = 1;
static const char* SENSOR_LABELS_NVS_NAMESPACE = "sensor_labels";
static const char* SENSOR_LABELS_NVS_KEY = "labels_blob";
static constexpr uint16_t IRRIGATION_CONFIG_NVS_VERSION = 3;
static const char* IRRIGATION_CONFIG_NVS_NAMESPACE = "irrigation_cfg";
static const char* IRRIGATION_CONFIG_NVS_KEY = "config_blob";
static const char* IRRIGATION_LEASE_ID_NVS_KEY = "lease_id";
static constexpr uint32_t IRRIGATION_SYNC_PERIOD_MS = 2000;
static constexpr uint32_t IRRIGATION_LEASE_HORIZON_MS = 120000;

struct SensorLabelRecord {
  uint16_t nodeId;
  char name[SENSOR_NAME_MAX];
};

struct SensorLabelsNvsBlob {
  uint16_t version;
  uint16_t reserved;
  SensorLabelRecord records[MAX_SENSOR_LABELS];
};

struct SensorLabel {
  bool used;
  uint16_t nodeId;
  char name[SENSOR_NAME_MAX];
};

static SensorLabel s_sensorLabels[MAX_SENSOR_LABELS] = {};
static Preferences s_sensorLabelsPrefs;
static bool s_sensorLabelsPrefsReady = false;
static Preferences s_irrigationPrefs;
static bool s_irrigationPrefsReady = false;

enum IrrigationMode : uint8_t {
  IRRIGATION_MODE_AUTO = 0,
  IRRIGATION_MODE_MANUAL = 1,
  IRRIGATION_MODE_OFF = 2,
  IRRIGATION_MODE_TIME = 3,
};

struct IrrigationConfigNvsBlob {
  uint16_t version;
  uint8_t mode;
  uint8_t reserved0;
  uint16_t autoStartPermille;
  uint16_t autoStopPermille;
  uint16_t manualDurationSec;
  uint16_t timeIntervalMin;
  uint16_t timeRunDurationSec;
  uint16_t reserved1;
};

static constexpr uint16_t AUTO_START_DEFAULT_PERMILLE = 350;
static constexpr uint16_t AUTO_STOP_DEFAULT_PERMILLE = 450;
static constexpr uint16_t MANUAL_DURATION_DEFAULT_SEC = 120;
static constexpr uint16_t MANUAL_DURATION_MIN_SEC = 0;
static constexpr uint16_t MANUAL_DURATION_MAX_SEC = 600;
static constexpr uint16_t TIME_INTERVAL_DEFAULT_MIN = 360;
static constexpr uint16_t TIME_INTERVAL_MIN = 5;
static constexpr uint16_t TIME_INTERVAL_MAX = 1440;
static constexpr uint16_t TIME_RUN_DEFAULT_SEC = 300;
static constexpr uint16_t TIME_RUN_MIN_SEC = 0;
static constexpr uint16_t TIME_RUN_MAX_SEC = 600;

static IrrigationMode s_irrigationMode = IRRIGATION_MODE_AUTO;
static bool s_manualIrrigationActive = false;
static uint16_t s_autoStartPermille = AUTO_START_DEFAULT_PERMILLE;
static uint16_t s_autoStopPermille = AUTO_STOP_DEFAULT_PERMILLE;
static uint16_t s_manualDurationSec = MANUAL_DURATION_DEFAULT_SEC;
static uint16_t s_timeIntervalMin = TIME_INTERVAL_DEFAULT_MIN;
static uint16_t s_timeRunDurationSec = TIME_RUN_DEFAULT_SEC;
static uint32_t s_manualRunDeadlineMs = 0;
static uint32_t s_requestedRunDurationSec = 0;
static uint32_t s_timeNextStartMs = 0;
static uint32_t s_timeCycleStartMs = 0;
static bool s_irrigationSyncDirty = true;
static uint32_t s_lastIrrigationSyncMs = 0;
static uint64_t s_irrigationLeaseId = 1;
static bool s_lastLeaseDesiredActiveInitialized = false;
static bool s_lastLeaseDesiredActive = false;

enum ControlCommandPhase : uint8_t {
  CONTROL_CMD_IDLE = 0,
  CONTROL_CMD_PENDING_START = 1,
  CONTROL_CMD_ACTIVE = 2,
  CONTROL_CMD_PENDING_STOP = 3,
  CONTROL_CMD_LOST = 4,
};

struct PendingControlCommand {
  bool active;
  uint16_t cmdId;
  uint8_t action;
  bool receivedAck;
  uint32_t sentAtMs;
  uint32_t hardDeadlineMs;
  uint32_t softDeadlineMs;
  uint32_t nextRetryAtMs;
  uint8_t retriesUsed;
  uint32_t nextStatusProbeAtMs;
};

static ControlCommandPhase s_controlPhase = CONTROL_CMD_IDLE;
static PendingControlCommand s_pendingControlCmd = {};
static uint16_t s_nextControlCmdId = 1;
static bool s_controlConfirmedIrrigationActive = false;

static constexpr uint32_t CONTROL_CMD_HARD_TIMEOUT_MS = 30000;
static constexpr uint32_t CONTROL_CMD_SOFT_TIMEOUT_MS = 2500;
static constexpr uint32_t CONTROL_STATUS_PROBE_INTERVAL_MS = 1200;
static constexpr uint32_t CONTROL_DESIRED_RECONCILE_MS = 2000;
static constexpr uint32_t CONTROL_CMD_RETRY_DELAYS_MS[3] = {180, 500, 1200};
static constexpr uint32_t CONTROL_ACK_PRESENCE_GRACE_MS = 7000;
static uint32_t s_nextControlDesiredReconcileAtMs = 0;
static uint32_t s_lastControlAckAtMs = 0;

static WiFiClient s_sseClient;
static bool s_sseClientActive = false;
static char s_lastSseSnapshotJson[8192] = {0};
static char s_sseSnapshotJson[8192] = {0};
static char s_sseWebStatusJson[320] = {0};
static char s_sseUnitStatusJson[320] = {0};
static char s_sseIrrigationConfigJson[640] = {0};
static char s_sseSystemSummaryJson[384] = {0};
static char s_sseNodesJson[4096] = {0};
static uint32_t s_nextSseHeartbeatMs = 0;
static constexpr uint32_t SSE_HEARTBEAT_MS = 10000;

static bool saveIrrigationLeaseIdToNvs()
{
  if (!s_irrigationPrefsReady || s_irrigationLeaseId == 0) {
    return false;
  }
  return s_irrigationPrefs.putBytes(IRRIGATION_LEASE_ID_NVS_KEY, &s_irrigationLeaseId, sizeof(s_irrigationLeaseId)) == sizeof(s_irrigationLeaseId);
}

static void loadIrrigationLeaseIdFromNvs()
{
  s_irrigationLeaseId = 1;
  if (!s_irrigationPrefsReady) {
    return;
  }

  if (!s_irrigationPrefs.isKey(IRRIGATION_LEASE_ID_NVS_KEY)) {
    return;
  }

  uint64_t stored64 = 0;
  const size_t read = s_irrigationPrefs.getBytes(IRRIGATION_LEASE_ID_NVS_KEY, &stored64, sizeof(stored64));
  if (read == sizeof(stored64) && stored64 != 0) {
    s_irrigationLeaseId = stored64;
    return;
  }

  const uint32_t legacy32 = s_irrigationPrefs.getULong(IRRIGATION_LEASE_ID_NVS_KEY, 1);
  s_irrigationLeaseId = (legacy32 == 0) ? 1 : static_cast<uint64_t>(legacy32);
}

static const char* nodeStateToText(TelemetryHeadNodeState state)
{
  switch (state) {
    case TELEMETRY_HEAD_NODE_ONLINE:
      return "ONLINE";
    case TELEMETRY_HEAD_NODE_SUSPECT:
      return "SUSPECT";
    case TELEMETRY_HEAD_NODE_OFFLINE:
      return "OFFLINE";
    default:
      return "UNKNOWN";
  }
}

static const char* batteryStateToText(TelemetryHeadBatteryState state)
{
  switch (state) {
    case TELEMETRY_HEAD_BATTERY_OK:
      return "OK";
    case TELEMETRY_HEAD_BATTERY_CRITICAL:
      return "CRITICAL";
    case TELEMETRY_HEAD_BATTERY_NEEDS_REPLACEMENT:
      return "NEEDS_REPLACEMENT";
    default:
      return "UNKNOWN";
  }
}

static const char* nodeRoleToText(bool isControl)
{
  return isControl ? "CONTROL" : "SENSOR";
}

static const char* irrigationLockoutToText(bool lowBatteryLockout)
{
  return lowBatteryLockout ? "LOW_BATTERY" : "NONE";
}

static const char* irrigationModeToText(IrrigationMode mode)
{
  switch (mode) {
    case IRRIGATION_MODE_AUTO:
      return "AUTO";
    case IRRIGATION_MODE_MANUAL:
      return "MANUAL";
    case IRRIGATION_MODE_OFF:
      return "OFF";
    case IRRIGATION_MODE_TIME:
      return "TIME";
    default:
      return "AUTO";
  }
}

static const char* controlPhaseToText(ControlCommandPhase phase)
{
  switch (phase) {
    case CONTROL_CMD_IDLE:
      return "idle";
    case CONTROL_CMD_PENDING_START:
      return "pending_start";
    case CONTROL_CMD_ACTIVE:
      return "active";
    case CONTROL_CMD_PENDING_STOP:
      return "pending_stop";
    case CONTROL_CMD_LOST:
      return "lost";
    default:
      return "idle";
  }
}

static uint16_t clampU16(uint16_t value, uint16_t minValue, uint16_t maxValue)
{
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

static void normalizeIrrigationConfig()
{
  s_autoStartPermille = clampU16(s_autoStartPermille, 0, 1000);
  s_autoStopPermille = clampU16(s_autoStopPermille, 0, 1000);
  if (s_autoStopPermille <= s_autoStartPermille) {
    s_autoStopPermille = clampU16(static_cast<uint16_t>(s_autoStartPermille + 50), 0, 1000);
    if (s_autoStopPermille <= s_autoStartPermille) {
      s_autoStartPermille = (s_autoStopPermille > 0) ? static_cast<uint16_t>(s_autoStopPermille - 1) : 0;
    }
  }

  s_manualDurationSec = clampU16(s_manualDurationSec, MANUAL_DURATION_MIN_SEC, MANUAL_DURATION_MAX_SEC);
  s_timeIntervalMin = clampU16(s_timeIntervalMin, TIME_INTERVAL_MIN, TIME_INTERVAL_MAX);
  s_timeRunDurationSec = clampU16(s_timeRunDurationSec, TIME_RUN_MIN_SEC, TIME_RUN_MAX_SEC);
}

static bool parseIrrigationModeArg(const String& value, IrrigationMode* outMode)
{
  if (!outMode) {
    return false;
  }
  if (value.equalsIgnoreCase("AUTO")) {
    *outMode = IRRIGATION_MODE_AUTO;
    return true;
  }
  if (value.equalsIgnoreCase("MANUAL")) {
    *outMode = IRRIGATION_MODE_MANUAL;
    return true;
  }
  if (value.equalsIgnoreCase("OFF")) {
    *outMode = IRRIGATION_MODE_OFF;
    return true;
  }
  if (value.equalsIgnoreCase("TIME")) {
    *outMode = IRRIGATION_MODE_TIME;
    return true;
  }
  return false;
}

static bool desiredIrrigationActive()
{
  return s_manualIrrigationActive;
}

static void stopIrrigation(const char* reason);
static void markIrrigationSyncDirty();
static void resolveSensorName(uint16_t nodeId, char outName[SENSOR_NAME_MAX]);

static bool computeAverageOnlineSensorMoisture(uint16_t* outPermille)
{
  if (!outPermille) {
    return false;
  }

  TelemetryHeadNodePresence nodes[8] = {};
  const uint8_t count = telemetryHeadGetPresence(nodes, 8);
  uint32_t sumPermille = 0;
  uint16_t onlineSensors = 0;

  for (uint8_t i = 0; i < count; ++i) {
    const TelemetryHeadNodePresence& node = nodes[i];
    if (node.isControl || node.state != TELEMETRY_HEAD_NODE_ONLINE) {
      continue;
    }
    sumPermille += static_cast<uint32_t>(node.moisturePermille);
    ++onlineSensors;
  }

  if (onlineSensors == 0) {
    return false;
  }

  *outPermille = static_cast<uint16_t>(sumPermille / onlineSensors);
  return true;
}

static bool hasControlOnlinePresence()
{
  TelemetryHeadNodePresence nodes[8] = {};
  const uint8_t count = telemetryHeadGetPresence(nodes, 8);
  for (uint8_t i = 0; i < count; ++i) {
    if (nodes[i].isControl && nodes[i].state == TELEMETRY_HEAD_NODE_ONLINE) {
      return true;
    }
  }
  return false;
}

static bool hasControlReachablePresence(uint32_t nowMs)
{
  TelemetryHeadNodePresence nodes[8] = {};
  const uint8_t count = telemetryHeadGetPresence(nodes, 8);
  for (uint8_t i = 0; i < count; ++i) {
    const TelemetryHeadNodePresence& node = nodes[i];
    if (!node.isControl || node.state != TELEMETRY_HEAD_NODE_ONLINE) {
      continue;
    }

    const bool sleepActive = node.sleepAcked &&
                             node.sleepExpectedReportDeadlineMs != 0 &&
                             (int32_t)(node.sleepExpectedReportDeadlineMs - nowMs) > 0;
    if (!sleepActive) {
      return true;
    }
  }
  return false;
}

static bool sendControlCommandOnce(uint8_t action, uint16_t cmdId)
{
  const bool sent = telemetryHeadSendRemoteButtonAction(0, action, cmdId);
  Serial.print("OBS: control command sent=");
  Serial.print(sent ? 1 : 0);
  Serial.print(" action=");
  Serial.print((unsigned long)action);
  Serial.print(" cmdId=");
  Serial.println((unsigned long)cmdId);
  return sent;
}

static uint32_t currentIrrigationLeaseRemainingMs(uint32_t nowMs)
{
  if (!desiredIrrigationActive()) {
    return 0;
  }

  if (s_requestedRunDurationSec == 0) {
    return IRRIGATION_LEASE_HORIZON_MS;
  }

  if (s_manualRunDeadlineMs != 0) {
    if ((int32_t)(s_manualRunDeadlineMs - nowMs) > 0) {
      return static_cast<uint32_t>(s_manualRunDeadlineMs - nowMs);
    }
    return 0;
  }

  return s_requestedRunDurationSec * 1000UL;
}

static void beginPendingControlCommand(uint8_t action, uint32_t nowMs)
{
  s_pendingControlCmd.active = true;
  s_pendingControlCmd.cmdId = s_nextControlCmdId++;
  if (s_nextControlCmdId == 0) {
    s_nextControlCmdId = 1;
  }
  s_pendingControlCmd.action = action;
  s_pendingControlCmd.receivedAck = false;
  s_pendingControlCmd.sentAtMs = nowMs;
  s_pendingControlCmd.hardDeadlineMs = nowMs + CONTROL_CMD_HARD_TIMEOUT_MS;
  s_pendingControlCmd.softDeadlineMs = nowMs + CONTROL_CMD_SOFT_TIMEOUT_MS;
  s_pendingControlCmd.nextRetryAtMs = nowMs + CONTROL_CMD_RETRY_DELAYS_MS[0];
  s_pendingControlCmd.retriesUsed = 0;
  s_pendingControlCmd.nextStatusProbeAtMs = nowMs + CONTROL_STATUS_PROBE_INTERVAL_MS;
  (void)sendControlCommandOnce(action, s_pendingControlCmd.cmdId);
}

static void settleControlPhaseFromIrrigationState(uint8_t irrigationState)
{
  s_controlConfirmedIrrigationActive = (irrigationState == IRRIGATION_STATE_RUN);
  if (s_controlConfirmedIrrigationActive) {
    s_controlPhase = CONTROL_CMD_ACTIVE;
    if (s_requestedRunDurationSec > 0) {
      s_manualRunDeadlineMs = millis() + (s_requestedRunDurationSec * 1000UL);
    }
  } else {
    s_controlPhase = CONTROL_CMD_IDLE;
    s_manualRunDeadlineMs = 0;
  }
}

static void consumeControlCommandAcks(uint32_t nowMs)
{
  TelemetryHeadCommandAck ack{};
  while (telemetryHeadConsumeCommandAck(&ack)) {
    s_lastControlAckAtMs = nowMs;

    if (s_pendingControlCmd.active && ack.cmdId == s_pendingControlCmd.cmdId) {
      if (ack.status == COMMAND_ACK_STATUS_RECEIVED) {
        s_pendingControlCmd.receivedAck = true;
        s_pendingControlCmd.softDeadlineMs = nowMs + CONTROL_CMD_SOFT_TIMEOUT_MS;
      } else if (ack.status == COMMAND_ACK_STATUS_APPLIED) {
        settleControlPhaseFromIrrigationState(ack.irrigationState);
        s_pendingControlCmd.active = false;
      } else if (ack.status == COMMAND_ACK_STATUS_REJECTED) {
        s_pendingControlCmd.active = false;
        s_controlPhase = CONTROL_CMD_IDLE;
        s_controlConfirmedIrrigationActive = false;
        s_manualRunDeadlineMs = 0;
      }
      continue;
    }

    if (ack.status == COMMAND_ACK_STATUS_APPLIED &&
        ack.action == REMOTE_BUTTON_IRRIGATION_STATE_REQUEST) {
      settleControlPhaseFromIrrigationState(ack.irrigationState);
      if (s_pendingControlCmd.active) {
        const bool startSatisfied =
            s_pendingControlCmd.action == REMOTE_BUTTON_IRRIGATION_START &&
            ack.irrigationState == IRRIGATION_STATE_RUN;
        const bool stopSatisfied =
            s_pendingControlCmd.action == REMOTE_BUTTON_IRRIGATION_STOP &&
            ack.irrigationState == IRRIGATION_STATE_OFF;
        if (startSatisfied || stopSatisfied) {
          s_pendingControlCmd.active = false;
        }
      }
    }
  }
}

static void pendingControlCommandTick(uint32_t nowMs)
{
  if (!s_pendingControlCmd.active) {
    return;
  }

  if ((int32_t)(nowMs - s_pendingControlCmd.hardDeadlineMs) >= 0) {
    s_pendingControlCmd.active = false;
    s_controlPhase = CONTROL_CMD_LOST;
    s_controlConfirmedIrrigationActive = false;
    s_manualRunDeadlineMs = 0;
    if (s_manualIrrigationActive) {
      stopIrrigation("control command timeout");
      Serial.println("OBS: irrigation canceled due to control command timeout");
    }
    return;
  }

  if (s_pendingControlCmd.retriesUsed < 3 &&
      (int32_t)(nowMs - s_pendingControlCmd.nextRetryAtMs) >= 0) {
    const bool sent = sendControlCommandOnce(s_pendingControlCmd.action, s_pendingControlCmd.cmdId);
    s_pendingControlCmd.retriesUsed++;
    if (s_pendingControlCmd.retriesUsed < 3) {
      s_pendingControlCmd.nextRetryAtMs = nowMs + CONTROL_CMD_RETRY_DELAYS_MS[s_pendingControlCmd.retriesUsed];
    } else {
      s_pendingControlCmd.nextRetryAtMs = s_pendingControlCmd.hardDeadlineMs + 1;
    }
    if (sent && !s_pendingControlCmd.receivedAck) {
      s_pendingControlCmd.nextStatusProbeAtMs = nowMs + CONTROL_STATUS_PROBE_INTERVAL_MS;
    }
  }

  if ((s_pendingControlCmd.receivedAck || s_pendingControlCmd.retriesUsed >= 3) &&
      (int32_t)(nowMs - s_pendingControlCmd.softDeadlineMs) >= 0 &&
      (int32_t)(nowMs - s_pendingControlCmd.nextStatusProbeAtMs) >= 0) {
    (void)telemetryHeadSendRemoteButtonAction(0, REMOTE_BUTTON_IRRIGATION_STATE_REQUEST, 0);
    s_pendingControlCmd.nextStatusProbeAtMs = nowMs + CONTROL_STATUS_PROBE_INTERVAL_MS;
  }
}

static void reconcileControlToDesiredState(uint32_t nowMs)
{
  if (s_pendingControlCmd.active) {
    return;
  }

  if ((int32_t)(nowMs - s_nextControlDesiredReconcileAtMs) < 0) {
    return;
  }
  s_nextControlDesiredReconcileAtMs = nowMs + CONTROL_DESIRED_RECONCILE_MS;

  if (!hasControlReachablePresence(nowMs)) {
    return;
  }

  const bool desiredActive = desiredIrrigationActive();
  if (!desiredActive && s_controlConfirmedIrrigationActive) {
    s_controlPhase = CONTROL_CMD_PENDING_STOP;
    beginPendingControlCommand(REMOTE_BUTTON_IRRIGATION_STOP, nowMs);
    return;
  }

  if (desiredActive && !s_controlConfirmedIrrigationActive) {
    s_controlPhase = CONTROL_CMD_PENDING_START;
    beginPendingControlCommand(REMOTE_BUTTON_IRRIGATION_START, nowMs);
    return;
  }

  if (s_controlPhase == CONTROL_CMD_LOST) {
    (void)telemetryHeadSendRemoteButtonAction(0, REMOTE_BUTTON_IRRIGATION_STATE_REQUEST, 0);
  }
}

struct ControlAvailabilitySnapshot {
  const char* status;
  const char* manualBlockedReason;
  bool lowBatteryLockoutActive;
};

static ControlAvailabilitySnapshot computeControlAvailabilitySnapshot();

static size_t composeIrrigationConfigJson(char* body, size_t bodySize, uint32_t nowMs)
{
  if (!body || bodySize == 0) {
    return 0;
  }

  const ControlAvailabilitySnapshot controlSnapshot = computeControlAvailabilitySnapshot();
  uint32_t runRemainingSec = 0;
  if (s_controlConfirmedIrrigationActive && s_manualRunDeadlineMs != 0 && (int32_t)(s_manualRunDeadlineMs - nowMs) > 0) {
    runRemainingSec = static_cast<uint32_t>(s_manualRunDeadlineMs - nowMs) / 1000UL;
  }

  uint32_t timeNextStartSec = 0;
  if (s_irrigationMode == IRRIGATION_MODE_TIME && s_timeNextStartMs != 0 && (int32_t)(s_timeNextStartMs - nowMs) > 0) {
    timeNextStartSec = static_cast<uint32_t>(s_timeNextStartMs - nowMs) / 1000UL;
  }

  const uint32_t pendingElapsedSec = s_pendingControlCmd.active
      ? static_cast<uint32_t>(nowMs - s_pendingControlCmd.sentAtMs) / 1000UL
      : 0;

  const int written = snprintf(
      body,
      bodySize,
      "{\"mode\":\"%s\",\"manualActive\":%s,\"manualDurationSec\":%u,\"autoStartPermille\":%u,\"autoStopPermille\":%u,\"timeIntervalMin\":%u,\"timeRunDurationSec\":%u,\"timeNextStartSec\":%lu,\"runRemainingSec\":%lu,\"manualBlockedReason\":\"%s\",\"confirmedState\":\"%s\",\"pendingElapsedSec\":%lu,\"control\":{\"status\":\"%s\",\"nextWakeKnown\":false,\"nextWakeEtaSec\":null}}",
      irrigationModeToText(s_irrigationMode),
      s_controlConfirmedIrrigationActive ? "true" : "false",
      static_cast<unsigned>(s_manualDurationSec),
      static_cast<unsigned>(s_autoStartPermille),
      static_cast<unsigned>(s_autoStopPermille),
      static_cast<unsigned>(s_timeIntervalMin),
      static_cast<unsigned>(s_timeRunDurationSec),
      static_cast<unsigned long>(timeNextStartSec),
      static_cast<unsigned long>(runRemainingSec),
      controlSnapshot.manualBlockedReason,
      controlPhaseToText(s_controlPhase),
      static_cast<unsigned long>(pendingElapsedSec),
      controlSnapshot.status);

  if (written <= 0) {
    body[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written < static_cast<int>(bodySize) ? written : static_cast<int>(bodySize - 1));
}

static void sseCloseClient()
{
  if (s_sseClientActive) {
    s_sseClient.stop();
  }
  s_sseClientActive = false;
  s_lastSseSnapshotJson[0] = '\0';
}

static void onEventsSseApi()
{
  if (s_sseClientActive) {
    sseCloseClient();
  }

  s_sseClient = s_server.client();
  if (!s_sseClient) {
    return;
  }

  s_sseClient.setNoDelay(true);
  s_sseClient.print("HTTP/1.1 200 OK\r\n");
  s_sseClient.print("Content-Type: text/event-stream\r\n");
  s_sseClient.print("Cache-Control: no-cache\r\n");
  s_sseClient.print("Connection: keep-alive\r\n\r\n");
  s_sseClient.print("retry: 2000\n\n");

  s_sseClientActive = true;
  s_lastSseSnapshotJson[0] = '\0';
  s_nextSseHeartbeatMs = millis() + SSE_HEARTBEAT_MS;
}

static size_t composeNodesJson(char* body, size_t bodySize, uint32_t nowMs)
{
  if (!body || bodySize == 0) {
    return 0;
  }

  TelemetryHeadNodePresence nodes[8] = {};
  const uint8_t count = telemetryHeadGetPresence(nodes, 8);
  uint16_t pairedNodeIds[8] = {};
  uint8_t pairedNodeMacs[8][6] = {};
  const uint8_t pairedCount = pairingHeadGetPairedNodes(pairedNodeIds, pairedNodeMacs, 8);

  size_t offset = 0;
  offset += static_cast<size_t>(snprintf(body + offset, bodySize - offset, "["));

  for (uint8_t i = 0; i < count; ++i) {
    const TelemetryHeadNodePresence& node = nodes[i];
    char mac[18] = {0};
    char name[SENSOR_NAME_MAX] = {0};
    macToString(node.mac, mac, sizeof(mac));
    resolveSensorName(node.nodeId, name);
    const uint32_t lastSeenSecAgo = static_cast<uint32_t>(nowMs - node.lastSeenMs) / 1000;
    const bool sleepActive = node.sleepAcked &&
                             node.sleepExpectedReportDeadlineMs != 0 &&
                             (int32_t)(node.sleepExpectedReportDeadlineMs - nowMs) > 0;
    const uint32_t nextContactSec = sleepActive
      ? static_cast<uint32_t>(node.sleepExpectedReportDeadlineMs - nowMs) / 1000UL
      : 0;

    offset += static_cast<size_t>(snprintf(
        body + offset,
        bodySize - offset,
        "%s{\"slot\":%u,\"mac\":\"%s\",\"name\":\"%s\",\"nodeId\":%u,\"role\":\"%s\",\"state\":\"%s\",\"sleepActive\":%s,\"nextContactSec\":%lu,\"irrigationLockout\":\"%s\",\"moisturePermille\":%u,\"batteryEstMv\":%u,\"batteryState\":\"%s\",\"lastSeenSecAgo\":%lu,\"rxPackets\":%lu,\"rxDuplicates\":%lu,\"rxInvalid\":%lu,\"ackOkSent\":%lu,\"ackNotPairedSent\":%lu}",
        (i == 0) ? "" : ",",
        static_cast<unsigned>(i),
        mac,
        name,
        static_cast<unsigned>(node.nodeId),
        nodeRoleToText(node.isControl),
        nodeStateToText(node.state),
        sleepActive ? "true" : "false",
        static_cast<unsigned long>(nextContactSec),
        irrigationLockoutToText(node.lowBatteryLockout),
        static_cast<unsigned>(node.moisturePermille),
        static_cast<unsigned>(node.batteryEstMv),
        batteryStateToText(node.batteryState),
        static_cast<unsigned long>(lastSeenSecAgo),
        static_cast<unsigned long>(node.rxPackets),
        static_cast<unsigned long>(node.rxDuplicates),
        static_cast<unsigned long>(node.rxInvalid),
        static_cast<unsigned long>(node.ackOkSent),
        static_cast<unsigned long>(node.ackNotPairedSent)));

    if (offset >= bodySize - 2) {
      break;
    }
  }

  for (uint8_t i = 0; i < pairedCount; ++i) {
    const uint16_t nodeId = pairedNodeIds[i];
    bool alreadyPresent = false;
    for (uint8_t j = 0; j < count; ++j) {
      if (nodes[j].nodeId == nodeId) {
        alreadyPresent = true;
        break;
      }
    }
    if (alreadyPresent) {
      continue;
    }

    char mac[18] = {0};
    char name[SENSOR_NAME_MAX] = {0};
    macToString(pairedNodeMacs[i], mac, sizeof(mac));
    resolveSensorName(nodeId, name);

    offset += static_cast<size_t>(snprintf(
        body + offset,
        bodySize - offset,
      "%s{\"slot\":%u,\"mac\":\"%s\",\"name\":\"%s\",\"nodeId\":%u,\"role\":\"UNKNOWN\",\"state\":\"OFFLINE\",\"sleepActive\":false,\"nextContactSec\":0,\"irrigationLockout\":\"NONE\",\"moisturePermille\":0,\"batteryEstMv\":0,\"batteryState\":\"UNKNOWN\",\"lastSeenSecAgo\":0,\"rxPackets\":0,\"rxDuplicates\":0,\"rxInvalid\":0,\"ackOkSent\":0,\"ackNotPairedSent\":0}",
        (offset > 1) ? "," : "",
        static_cast<unsigned>(count + i),
        mac,
        name,
        static_cast<unsigned>(nodeId)));

    if (offset >= bodySize - 2) {
      break;
    }
  }

  (void)snprintf(body + offset, bodySize - offset, "]");
  return strlen(body);
}

static size_t composeSystemSummaryJson(char* body, size_t bodySize, uint32_t nowMs)
{
  if (!body || bodySize == 0) {
    return 0;
  }

  TelemetryHeadNodePresence nodes[8] = {};
  const uint8_t count = telemetryHeadGetPresence(nodes, 8);

  uint8_t onlineCount = 0;
  uint8_t suspectCount = 0;
  uint8_t offlineCount = 0;
  uint32_t moistureSumPermille = 0;

  for (uint8_t i = 0; i < count; ++i) {
    const TelemetryHeadNodePresence& node = nodes[i];
    if (node.state == TELEMETRY_HEAD_NODE_ONLINE) {
      ++onlineCount;
      moistureSumPermille += static_cast<uint32_t>(node.moisturePermille);
      continue;
    }
    if (node.state == TELEMETRY_HEAD_NODE_SUSPECT) {
      ++suspectCount;
      continue;
    }
    if (node.state == TELEMETRY_HEAD_NODE_OFFLINE) {
      ++offlineCount;
    }
  }

  const uint32_t avgMoisturePermille =
      (onlineCount > 0) ? (moistureSumPermille / static_cast<uint32_t>(onlineCount)) : 0;
  const bool hasMoistureAvg = (onlineCount > 0);
  const uint32_t pairingRemainingSec = (pairingHeadRemainingMs(nowMs) + 999UL) / 1000UL;
  const uint32_t uptimeSec = nowMs / 1000UL;
  char avgMoisture[16] = "null";
  if (hasMoistureAvg) {
    (void)snprintf(avgMoisture, sizeof(avgMoisture), "%lu", static_cast<unsigned long>(avgMoisturePermille));
  }

  const int written = snprintf(
      body,
      bodySize,
      "{\"onlineSensors\":%u,\"suspectSensors\":%u,\"offlineSensors\":%u,\"totalVisibleSensors\":%u,\"avgMoisturePermille\":%s,\"pairingOpen\":%s,\"pairingRemainingSec\":%lu,\"uptimeSec\":%lu,\"irrigationMode\":\"%s\",\"manualIrrigationActive\":%s}",
      static_cast<unsigned>(onlineCount),
      static_cast<unsigned>(suspectCount),
      static_cast<unsigned>(offlineCount),
      static_cast<unsigned>(count),
      avgMoisture,
      pairingHeadIsOpen() ? "true" : "false",
      static_cast<unsigned long>(pairingRemainingSec),
      static_cast<unsigned long>(uptimeSec),
      irrigationModeToText(s_irrigationMode),
      s_controlConfirmedIrrigationActive ? "true" : "false");

  if (written <= 0) {
    body[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written < static_cast<int>(bodySize) ? written : static_cast<int>(bodySize - 1));
}

static size_t composeDashboardSnapshotJson(char* body, size_t bodySize, uint32_t nowMs)
{
  if (!body || bodySize == 0) {
    return 0;
  }

  s_sseWebStatusJson[0] = '\0';
  s_sseUnitStatusJson[0] = '\0';
  s_sseIrrigationConfigJson[0] = '\0';
  s_sseSystemSummaryJson[0] = '\0';
  s_sseNodesJson[0] = '\0';

  (void)headProvisioningComposeWebStatusJson(s_sseWebStatusJson, sizeof(s_sseWebStatusJson), nowMs);
  (void)headProvisioningComposeUnitStatusJson(s_sseUnitStatusJson, sizeof(s_sseUnitStatusJson), nowMs);
  (void)composeIrrigationConfigJson(s_sseIrrigationConfigJson, sizeof(s_sseIrrigationConfigJson), nowMs);
  (void)composeSystemSummaryJson(s_sseSystemSummaryJson, sizeof(s_sseSystemSummaryJson), nowMs);
  (void)composeNodesJson(s_sseNodesJson, sizeof(s_sseNodesJson), nowMs);

  if (s_sseWebStatusJson[0] == '\0') {
    strlcpy(s_sseWebStatusJson, "{}", sizeof(s_sseWebStatusJson));
  }
  if (s_sseUnitStatusJson[0] == '\0') {
    strlcpy(s_sseUnitStatusJson, "{}", sizeof(s_sseUnitStatusJson));
  }
  if (s_sseIrrigationConfigJson[0] == '\0') {
    strlcpy(s_sseIrrigationConfigJson, "{}", sizeof(s_sseIrrigationConfigJson));
  }
  if (s_sseSystemSummaryJson[0] == '\0') {
    strlcpy(s_sseSystemSummaryJson, "{}", sizeof(s_sseSystemSummaryJson));
  }
  if (s_sseNodesJson[0] == '\0') {
    strlcpy(s_sseNodesJson, "[]", sizeof(s_sseNodesJson));
  }

  const int written = snprintf(
      body,
      bodySize,
      "{\"webStatus\":%s,\"nodes\":%s,\"summary\":%s,\"irrigationConfig\":%s,\"unitStatus\":%s}",
      s_sseWebStatusJson,
      s_sseNodesJson,
      s_sseSystemSummaryJson,
      s_sseIrrigationConfigJson,
      s_sseUnitStatusJson);

  if (written <= 0) {
    body[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written < static_cast<int>(bodySize) ? written : static_cast<int>(bodySize - 1));
}

static void ssePushSnapshotIfChanged(uint32_t nowMs)
{
  if (!s_sseClientActive) {
    return;
  }
  if (!s_sseClient.connected()) {
    sseCloseClient();
    return;
  }

  s_sseSnapshotJson[0] = '\0';
  (void)composeDashboardSnapshotJson(s_sseSnapshotJson, sizeof(s_sseSnapshotJson), nowMs);
  if (strcmp(s_sseSnapshotJson, s_lastSseSnapshotJson) != 0) {
    s_sseClient.print("event: snapshot\n");
    s_sseClient.print("data: ");
    s_sseClient.print(s_sseSnapshotJson);
    s_sseClient.print("\n\n");
    strncpy(s_lastSseSnapshotJson, s_sseSnapshotJson, sizeof(s_lastSseSnapshotJson) - 1);
    s_lastSseSnapshotJson[sizeof(s_lastSseSnapshotJson) - 1] = '\0';
    s_nextSseHeartbeatMs = nowMs + SSE_HEARTBEAT_MS;
    return;
  }

  if ((int32_t)(nowMs - s_nextSseHeartbeatMs) >= 0) {
    s_sseClient.print(": hb\n\n");
    s_nextSseHeartbeatMs = nowMs + SSE_HEARTBEAT_MS;
  }
}

static ControlAvailabilitySnapshot computeControlAvailabilitySnapshot()
{
  ControlAvailabilitySnapshot snapshot = {"not_paired", "control_not_paired", false};
  TelemetryHeadNodePresence nodes[8] = {};
  const uint8_t count = telemetryHeadGetPresence(nodes, 8);
  for (uint8_t i = 0; i < count; ++i) {
    const TelemetryHeadNodePresence& node = nodes[i];
    if (!node.isControl) {
      continue;
    }

    if (node.lowBatteryLockout) {
      snapshot.status = "battery_lockout";
      snapshot.manualBlockedReason = "control_battery_lockout";
      snapshot.lowBatteryLockoutActive = true;
      return snapshot;
    }

    if (node.state == TELEMETRY_HEAD_NODE_ONLINE) {
      snapshot.status = "online";
      snapshot.manualBlockedReason = "none";
      return snapshot;
    }

    snapshot.status = "offline";
    snapshot.manualBlockedReason = "control_offline";
    return snapshot;
  }

  return snapshot;
}

static void stopIrrigation(const char* reason)
{
  if (!s_manualIrrigationActive) {
    return;
  }
  s_manualIrrigationActive = false;
  s_timeCycleStartMs = 0;
  s_requestedRunDurationSec = 0;
  s_manualRunDeadlineMs = 0;
  markIrrigationSyncDirty();
  Serial.print("OBS: irrigation stopped");
  if (reason && reason[0] != '\0') {
    Serial.print(" (");
    Serial.print(reason);
    Serial.print(")");
  }
  Serial.println();
}

static void startIrrigation(uint32_t nowMs, uint32_t durationSec, const char* reason)
{
  (void)nowMs;
  if (s_manualIrrigationActive) {
    return;
  }
  s_manualIrrigationActive = true;
  s_requestedRunDurationSec = durationSec;
  s_manualRunDeadlineMs = 0;
  markIrrigationSyncDirty();
  Serial.print("OBS: irrigation started");
  if (durationSec > 0) {
    Serial.print(" durationSec=");
    Serial.print(static_cast<unsigned long>(durationSec));
  }
  if (reason && reason[0] != '\0') {
    Serial.print(" (");
    Serial.print(reason);
    Serial.print(")");
  }
  Serial.println();
}

static void irrigationAutomationTick(uint32_t nowMs)
{
  if (s_manualIrrigationActive &&
      s_controlConfirmedIrrigationActive &&
      !s_pendingControlCmd.active &&
      !hasControlOnlinePresence()) {
    s_controlPhase = CONTROL_CMD_LOST;
    s_controlConfirmedIrrigationActive = false;
    s_manualRunDeadlineMs = 0;
    stopIrrigation("control offline during irrigation");
    Serial.println("OBS: irrigation canceled due to control loss");
    return;
  }

  if (s_irrigationMode == IRRIGATION_MODE_TIME &&
      s_manualIrrigationActive &&
      !s_controlConfirmedIrrigationActive &&
      s_timeCycleStartMs != 0 &&
      s_requestedRunDurationSec > 0) {
    const uint32_t startupWindowMs = s_requestedRunDurationSec * 1000UL;
    if (static_cast<uint32_t>(nowMs - s_timeCycleStartMs) >= startupWindowMs) {
      if (s_pendingControlCmd.active || s_controlPhase == CONTROL_CMD_PENDING_START) {
        s_pendingControlCmd.active = false;
      }
      s_controlPhase = CONTROL_CMD_IDLE;
      s_controlConfirmedIrrigationActive = false;
      stopIrrigation("TIME cycle missed: no control confirmation");
      Serial.println("OBS: TIME cycle skipped (control did not confirm in-slot start)");
    }
  }

  if (s_manualIrrigationActive && s_controlConfirmedIrrigationActive &&
      s_manualRunDeadlineMs != 0 && (int32_t)(nowMs - s_manualRunDeadlineMs) >= 0) {
    stopIrrigation("duration elapsed");
    s_controlPhase = CONTROL_CMD_PENDING_STOP;
    beginPendingControlCommand(REMOTE_BUTTON_IRRIGATION_STOP, nowMs);
  }

  if (s_manualIrrigationActive) {
    return;
  }

  if (s_irrigationMode == IRRIGATION_MODE_OFF || s_irrigationMode == IRRIGATION_MODE_MANUAL) {
    return;
  }

  if (s_irrigationMode == IRRIGATION_MODE_AUTO) {
    uint16_t avgPermille = 0;
    if (!computeAverageOnlineSensorMoisture(&avgPermille)) {
      if (s_manualIrrigationActive) {
        stopIrrigation("AUTO no online sensors");
      }
      return;
    }

    if (!s_manualIrrigationActive && avgPermille <= s_autoStartPermille) {
      startIrrigation(nowMs, 0, "AUTO moisture below start");
      return;
    }

    if (s_manualIrrigationActive && avgPermille >= s_autoStopPermille) {
      stopIrrigation("AUTO moisture above stop");
    }
    return;
  }

  if (s_irrigationMode == IRRIGATION_MODE_TIME) {
    const uint32_t intervalMs = static_cast<uint32_t>(s_timeIntervalMin) * 60UL * 1000UL;
    const uint32_t runSec = static_cast<uint32_t>(s_timeRunDurationSec);

    if (s_timeNextStartMs == 0) {
      s_timeNextStartMs = nowMs + intervalMs;
      return;
    }

    if (!s_manualIrrigationActive && (int32_t)(nowMs - s_timeNextStartMs) >= 0) {
      if (!hasControlOnlinePresence()) {
        s_timeNextStartMs = nowMs + intervalMs;
        Serial.println("OBS: TIME cycle skipped (control offline)");
        return;
      }

      startIrrigation(nowMs, runSec, "TIME interval trigger");
      s_timeCycleStartMs = nowMs;
      s_timeNextStartMs = nowMs + intervalMs;
    }
  }
}

static bool parseUint16Arg(const char* key, uint16_t* outValue)
{
  if (!key || !outValue || !s_server.hasArg(key)) {
    return false;
  }

  const String value = s_server.arg(key);
  char* end = nullptr;
  const unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (end == value.c_str() || !end || *end != '\0' || parsed > 65535UL) {
    return false;
  }

  *outValue = static_cast<uint16_t>(parsed);
  return true;
}

static bool parseSizeArg(const char* key, size_t* outValue)
{
  if (!key || !outValue || !s_server.hasArg(key)) {
    return false;
  }

  const String value = s_server.arg(key);
  char* end = nullptr;
  const unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (end == value.c_str() || !end || *end != '\0') {
    return false;
  }

  *outValue = static_cast<size_t>(parsed);
  return true;
}

static void onTrackExportCsvApi()
{
  const size_t total = trackStorageSize();
  size_t offset = 0;
  size_t limit = total;

  if (s_server.hasArg("offset") && !parseSizeArg("offset", &offset)) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_offset\"}");
    return;
  }
  if (s_server.hasArg("limit") && !parseSizeArg("limit", &limit)) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_limit\"}");
    return;
  }

  if (offset > total) {
    offset = total;
  }
  const size_t available = total - offset;
  if (limit > available) {
    limit = available;
  }

  s_server.sendHeader("Cache-Control", "no-store");
  s_server.sendHeader("Content-Disposition", "attachment; filename=track_export.csv");
  s_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_server.send(200, "text/csv; charset=utf-8", "");

  char line[256] = {0};
  (void)snprintf(line, sizeof(line), "# head_uptime_ms=%lu\n", static_cast<unsigned long>(millis()));
  s_server.sendContent(line);
  (void)snprintf(line, sizeof(line), "# total_records=%u\n", static_cast<unsigned>(total));
  s_server.sendContent(line);
  (void)snprintf(line, sizeof(line), "# offset=%u\n", static_cast<unsigned>(offset));
  s_server.sendContent(line);
  (void)snprintf(line, sizeof(line), "# limit=%u\n", static_cast<unsigned>(limit));
  s_server.sendContent(line);
  s_server.sendContent("seq,ts_ms,node_id,telemetry_seq,moisture_permille,moisture_raw_mv,battery_raw_mv,battery_est_mv,flags,mac\n");

  static constexpr size_t CSV_CHUNK = 64;
  TrackRecord buffer[CSV_CHUNK] = {};
  size_t emitted = 0;

  while (emitted < limit) {
    const size_t chunkMax = ((limit - emitted) < CSV_CHUNK) ? (limit - emitted) : CSV_CHUNK;
    const size_t copied = trackStorageCopyWindow(buffer, chunkMax, offset + emitted);
    if (copied == 0) {
      break;
    }

    for (size_t i = 0; i < copied; ++i) {
      const TrackRecord& rec = buffer[i];
      (void)snprintf(
          line,
          sizeof(line),
          "%lu,%lu,%u,%u,%u,%u,%u,%u,%u,%02X:%02X:%02X:%02X:%02X:%02X\n",
          static_cast<unsigned long>(rec.seq),
          static_cast<unsigned long>(rec.tsMs),
          static_cast<unsigned>(rec.nodeId),
          static_cast<unsigned>(rec.telemetrySeq),
          static_cast<unsigned>(rec.moisturePermille),
          static_cast<unsigned>(rec.moistureRawMv),
          static_cast<unsigned>(rec.batteryRawMv),
          static_cast<unsigned>(rec.batteryEstMv),
          static_cast<unsigned>(rec.flags),
          rec.mac[0],
          rec.mac[1],
          rec.mac[2],
          rec.mac[3],
          rec.mac[4],
          rec.mac[5]);
      s_server.sendContent(line);
    }

    emitted += copied;
  }

  s_server.sendContent("");
}

static bool sendDesiredIrrigationState()
{
  const uint32_t nowMs = millis();
  const bool desiredActive = desiredIrrigationActive();
  if (!s_lastLeaseDesiredActiveInitialized) {
    s_lastLeaseDesiredActive = desiredActive;
    s_lastLeaseDesiredActiveInitialized = true;
  } else if (desiredActive != s_lastLeaseDesiredActive) {
    s_lastLeaseDesiredActive = desiredActive;
    s_irrigationLeaseId = (s_irrigationLeaseId == 0xFFFFFFFFFFFFFFFFull) ? 1ull : (s_irrigationLeaseId + 1ull);
    if (!saveIrrigationLeaseIdToNvs()) {
      Serial.println("OBS: warning, irrigation lease id not persisted");
    }
  }

  const uint8_t desiredState = desiredActive ? IRRIGATION_STATE_RUN : IRRIGATION_STATE_OFF;
  const uint32_t remainingLeaseMs = currentIrrigationLeaseRemainingMs(nowMs);
  return telemetryHeadSendIrrigationState(desiredState, s_irrigationLeaseId, remainingLeaseMs);
}

static void markIrrigationSyncDirty()
{
  s_irrigationSyncDirty = true;
}

static void irrigationSyncTick(uint32_t nowMs)
{
  const bool periodicDue =
      s_lastIrrigationSyncMs == 0 ||
      static_cast<uint32_t>(nowMs - s_lastIrrigationSyncMs) >= IRRIGATION_SYNC_PERIOD_MS;

  if (!s_irrigationSyncDirty && !periodicDue) {
    return;
  }

  if (sendDesiredIrrigationState()) {
    s_lastIrrigationSyncMs = nowMs;
    s_irrigationSyncDirty = false;
  }
}

static int8_t findSensorLabelSlot(uint16_t nodeId)
{
  for (uint8_t i = 0; i < MAX_SENSOR_LABELS; ++i) {
    if (s_sensorLabels[i].used && s_sensorLabels[i].nodeId == nodeId) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
}

static int8_t findFreeSensorLabelSlot()
{
  for (uint8_t i = 0; i < MAX_SENSOR_LABELS; ++i) {
    if (!s_sensorLabels[i].used) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
}

static bool saveSensorLabelsToNvs()
{
  if (!s_sensorLabelsPrefsReady) {
    return false;
  }

  SensorLabelsNvsBlob blob{};
  blob.version = SENSOR_LABELS_NVS_VERSION;
  for (uint8_t i = 0; i < MAX_SENSOR_LABELS; ++i) {
    if (!s_sensorLabels[i].used || s_sensorLabels[i].nodeId == 0) {
      continue;
    }
    blob.records[i].nodeId = s_sensorLabels[i].nodeId;
    strlcpy(blob.records[i].name, s_sensorLabels[i].name, sizeof(blob.records[i].name));
  }

  const size_t written = s_sensorLabelsPrefs.putBytes(SENSOR_LABELS_NVS_KEY, &blob, sizeof(blob));
  return written == sizeof(blob);
}

static void loadSensorLabelsFromNvs()
{
  if (!s_sensorLabelsPrefsReady) {
    return;
  }

  if (!s_sensorLabelsPrefs.isKey(SENSOR_LABELS_NVS_KEY)) {
    memset(s_sensorLabels, 0, sizeof(s_sensorLabels));
    return;
  }

  SensorLabelsNvsBlob blob{};
  const size_t read = s_sensorLabelsPrefs.getBytes(SENSOR_LABELS_NVS_KEY, &blob, sizeof(blob));
  if (read != sizeof(blob) || blob.version != SENSOR_LABELS_NVS_VERSION) {
    memset(s_sensorLabels, 0, sizeof(s_sensorLabels));
    return;
  }

  memset(s_sensorLabels, 0, sizeof(s_sensorLabels));
  for (uint8_t i = 0; i < MAX_SENSOR_LABELS; ++i) {
    if (blob.records[i].nodeId == 0 || blob.records[i].name[0] == '\0') {
      continue;
    }
    s_sensorLabels[i].used = true;
    s_sensorLabels[i].nodeId = blob.records[i].nodeId;
    strlcpy(s_sensorLabels[i].name, blob.records[i].name, sizeof(s_sensorLabels[i].name));
  }
}

static bool saveIrrigationConfigToNvs()
{
  if (!s_irrigationPrefsReady) {
    return false;
  }

  IrrigationConfigNvsBlob blob{};
  blob.version = IRRIGATION_CONFIG_NVS_VERSION;
  blob.mode = static_cast<uint8_t>(s_irrigationMode);
  blob.autoStartPermille = s_autoStartPermille;
  blob.autoStopPermille = s_autoStopPermille;
  blob.manualDurationSec = s_manualDurationSec;
  blob.timeIntervalMin = s_timeIntervalMin;
  blob.timeRunDurationSec = s_timeRunDurationSec;
  const size_t written = s_irrigationPrefs.putBytes(IRRIGATION_CONFIG_NVS_KEY, &blob, sizeof(blob));
  return written == sizeof(blob);
}

static void loadIrrigationConfigFromNvs()
{
  s_irrigationMode = IRRIGATION_MODE_AUTO;
  s_autoStartPermille = AUTO_START_DEFAULT_PERMILLE;
  s_autoStopPermille = AUTO_STOP_DEFAULT_PERMILLE;
  s_manualDurationSec = MANUAL_DURATION_DEFAULT_SEC;
  s_timeIntervalMin = TIME_INTERVAL_DEFAULT_MIN;
  s_timeRunDurationSec = TIME_RUN_DEFAULT_SEC;
  if (!s_irrigationPrefsReady) {
    return;
  }

  if (!s_irrigationPrefs.isKey(IRRIGATION_CONFIG_NVS_KEY)) {
    return;
  }

  uint8_t raw[sizeof(IrrigationConfigNvsBlob)] = {0};
  const size_t read = s_irrigationPrefs.getBytes(IRRIGATION_CONFIG_NVS_KEY, raw, sizeof(raw));
  if (read < 4) {
    return;
  }

  const uint16_t version = static_cast<uint16_t>(raw[0] | (raw[1] << 8));
  const uint8_t mode = raw[2];
  if (mode <= IRRIGATION_MODE_TIME) {
    s_irrigationMode = static_cast<IrrigationMode>(mode);
  }

  if (version == 1) {
    normalizeIrrigationConfig();
    return;
  }

  if (version == 2 && read >= sizeof(IrrigationConfigNvsBlob)) {
    const IrrigationConfigNvsBlob* blob = reinterpret_cast<const IrrigationConfigNvsBlob*>(raw);
    s_autoStartPermille = blob->autoStartPermille;
    s_autoStopPermille = blob->autoStopPermille;
    s_manualDurationSec = blob->manualDurationSec;
    s_timeIntervalMin = blob->timeIntervalMin;
    s_timeRunDurationSec = static_cast<uint16_t>(blob->timeRunDurationSec * 60U);
    normalizeIrrigationConfig();
    return;
  }

  if (version != IRRIGATION_CONFIG_NVS_VERSION || read < sizeof(IrrigationConfigNvsBlob)) {
    normalizeIrrigationConfig();
    return;
  }

  const IrrigationConfigNvsBlob* blob = reinterpret_cast<const IrrigationConfigNvsBlob*>(raw);
  s_autoStartPermille = blob->autoStartPermille;
  s_autoStopPermille = blob->autoStopPermille;
  s_manualDurationSec = blob->manualDurationSec;
  s_timeIntervalMin = blob->timeIntervalMin;
  s_timeRunDurationSec = blob->timeRunDurationSec;
  normalizeIrrigationConfig();
}

static bool sanitizeSensorName(const String& input, char outName[SENSOR_NAME_MAX])
{
  if (!outName) {
    return false;
  }

  String raw = input;
  raw.trim();
  if (raw.length() == 0) {
    return false;
  }

  size_t outLen = 0;
  for (size_t i = 0; i < raw.length() && outLen < (SENSOR_NAME_MAX - 1); ++i) {
    const char ch = raw[i];
    if (isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '_' || ch == '-') {
      outName[outLen++] = ch;
    }
  }

  outName[outLen] = '\0';
  return outLen > 0;
}

static bool setSensorLabel(uint16_t nodeId, const char* name)
{
  if (nodeId == 0 || !name || name[0] == '\0') {
    return false;
  }

  int8_t slot = findSensorLabelSlot(nodeId);
  if (slot < 0) {
    slot = findFreeSensorLabelSlot();
  }
  if (slot < 0) {
    return false;
  }

  s_sensorLabels[slot].used = true;
  s_sensorLabels[slot].nodeId = nodeId;
  strlcpy(s_sensorLabels[slot].name, name, sizeof(s_sensorLabels[slot].name));
  if (!saveSensorLabelsToNvs()) {
    Serial.println("OBS: warning, sensor label not persisted");
  }
  return true;
}

static void clearSensorLabel(uint16_t nodeId)
{
  const int8_t slot = findSensorLabelSlot(nodeId);
  if (slot < 0) {
    return;
  }
  memset(&s_sensorLabels[slot], 0, sizeof(s_sensorLabels[slot]));
  (void)saveSensorLabelsToNvs();
}

static void resolveSensorName(uint16_t nodeId, char outName[SENSOR_NAME_MAX])
{
  if (!outName) {
    return;
  }

  const int8_t slot = findSensorLabelSlot(nodeId);
  if (slot >= 0) {
    strlcpy(outName, s_sensorLabels[slot].name, SENSOR_NAME_MAX);
    return;
  }

  (void)snprintf(outName, SENSOR_NAME_MAX, "Sensor %u", static_cast<unsigned>(nodeId));
}

static bool parseNodeIdArg(uint16_t* outNodeId)
{
  if (!outNodeId || !s_server.hasArg("nodeId")) {
    return false;
  }

  const String nodeIdArg = s_server.arg("nodeId");
  char* end = nullptr;
  const unsigned long parsed = strtoul(nodeIdArg.c_str(), &end, 10);
  if (end == nodeIdArg.c_str() || !end || *end != '\0') {
    return false;
  }
  if (parsed == 0 || parsed > 65535UL) {
    return false;
  }

  *outNodeId = static_cast<uint16_t>(parsed);
  return true;
}

static void onSensorRenameApi()
{
  uint16_t nodeId = 0;
  if (!parseNodeIdArg(&nodeId) || !s_server.hasArg("name")) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_args\"}");
    return;
  }

  char sanitized[SENSOR_NAME_MAX] = {0};
  if (!sanitizeSensorName(s_server.arg("name"), sanitized)) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_name\"}");
    return;
  }

  if (!setSensorLabel(nodeId, sanitized)) {
    s_server.send(500, "application/json", "{\"ok\":0,\"error\":\"label_storage_full\"}");
    return;
  }

  s_server.send(200, "application/json", "{\"ok\":1}");
}

static void onSensorUnpairApi()
{
  uint16_t nodeId = 0;
  if (!parseNodeIdArg(&nodeId)) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_nodeId\"}");
    return;
  }

  if (!pairingHeadUnpairNode(nodeId)) {
    s_server.send(404, "application/json", "{\"ok\":0,\"error\":\"not_found\"}");
    return;
  }

  (void)telemetryHeadRemovePresenceByNodeId(nodeId);
  clearSensorLabel(nodeId);
  s_server.send(200, "application/json", "{\"ok\":1}");
}

static void onSensorCalibrateApi()
{
  uint16_t nodeId = 0;
  if (!parseNodeIdArg(&nodeId) || !s_server.hasArg("step")) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_args\"}");
    return;
  }

  const String step = s_server.arg("step");
  uint8_t action = 0;
  if (step == "start") {
    action = REMOTE_BUTTON_CALIBRATE_START;
  } else if (step == "wet") {
    action = REMOTE_BUTTON_CALIBRATE_MEASURE_WET;
  } else {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_step\"}");
    return;
  }

  TelemetryHeadNodePresence nodes[8] = {};
  const uint8_t count = telemetryHeadGetPresence(nodes, 8);
  const TelemetryHeadNodePresence* targetNode = nullptr;
  for (uint8_t i = 0; i < count; ++i) {
    if (nodes[i].nodeId == nodeId) {
      targetNode = &nodes[i];
      break;
    }
  }

  if (!targetNode) {
    s_server.send(404, "application/json", "{\"ok\":0,\"error\":\"node_not_found\"}");
    return;
  }

  if (targetNode->state != TELEMETRY_HEAD_NODE_ONLINE) {
    s_server.send(409, "application/json", "{\"ok\":0,\"error\":\"node_not_online\"}");
    return;
  }

  if (!telemetryHeadSendRemoteButtonAction(nodeId, action)) {
    s_server.send(404, "application/json", "{\"ok\":0,\"error\":\"node_not_available\"}");
    return;
  }

  s_server.send(200, "application/json", "{\"ok\":1}");
}

static void onIrrigationConfigGetApi()
{
  const uint32_t nowMs = millis();
  char body[640] = {0};
  (void)composeIrrigationConfigJson(body, sizeof(body), nowMs);
  s_server.send(200, "application/json", body);
}

static void onIrrigationConfigPostApi()
{
  if (!s_server.hasArg("mode")) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_args\"}");
    return;
  }

  IrrigationMode requestedMode = IRRIGATION_MODE_AUTO;
  if (!parseIrrigationModeArg(s_server.arg("mode"), &requestedMode)) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_mode\"}");
    return;
  }

  uint16_t requestedManualDurationSec = s_manualDurationSec;
  uint16_t requestedAutoStartPermille = s_autoStartPermille;
  uint16_t requestedAutoStopPermille = s_autoStopPermille;
  uint16_t requestedTimeIntervalMin = s_timeIntervalMin;
  uint16_t requestedTimeRunDurationSec = s_timeRunDurationSec;

  if (s_server.hasArg("manualDurationSec") && !parseUint16Arg("manualDurationSec", &requestedManualDurationSec)) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_manualDurationSec\"}");
    return;
  }
  if (s_server.hasArg("autoStartPermille") && !parseUint16Arg("autoStartPermille", &requestedAutoStartPermille)) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_autoStartPermille\"}");
    return;
  }
  if (s_server.hasArg("autoStopPermille") && !parseUint16Arg("autoStopPermille", &requestedAutoStopPermille)) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_autoStopPermille\"}");
    return;
  }
  if (s_server.hasArg("timeIntervalMin") && !parseUint16Arg("timeIntervalMin", &requestedTimeIntervalMin)) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_timeIntervalMin\"}");
    return;
  }
  if (s_server.hasArg("timeRunDurationSec") && !parseUint16Arg("timeRunDurationSec", &requestedTimeRunDurationSec)) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_timeRunDurationSec\"}");
    return;
  }
  if (s_server.hasArg("timeRunDurationMin")) {
    uint16_t legacyTimeRunDurationMin = 0;
    if (!parseUint16Arg("timeRunDurationMin", &legacyTimeRunDurationMin)) {
      s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_timeRunDurationMin\"}");
      return;
    }
    requestedTimeRunDurationSec = static_cast<uint16_t>(legacyTimeRunDurationMin * 60U);
  }

  requestedManualDurationSec = clampU16(requestedManualDurationSec, MANUAL_DURATION_MIN_SEC, MANUAL_DURATION_MAX_SEC);
  requestedAutoStartPermille = clampU16(requestedAutoStartPermille, 0, 1000);
  requestedAutoStopPermille = clampU16(requestedAutoStopPermille, 0, 1000);
  if (requestedAutoStopPermille <= requestedAutoStartPermille) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"auto_stop_must_be_above_start\"}");
    return;
  }
  requestedTimeIntervalMin = clampU16(requestedTimeIntervalMin, TIME_INTERVAL_MIN, TIME_INTERVAL_MAX);
  requestedTimeRunDurationSec = clampU16(requestedTimeRunDurationSec, TIME_RUN_MIN_SEC, TIME_RUN_MAX_SEC);

  s_irrigationMode = requestedMode;
  s_manualDurationSec = requestedManualDurationSec;
  s_autoStartPermille = requestedAutoStartPermille;
  s_autoStopPermille = requestedAutoStopPermille;
  s_timeIntervalMin = requestedTimeIntervalMin;
  s_timeRunDurationSec = requestedTimeRunDurationSec;
  if (s_irrigationMode == IRRIGATION_MODE_TIME) {
    s_timeNextStartMs = millis() + static_cast<uint32_t>(s_timeIntervalMin) * 60UL * 1000UL;
  } else {
    s_timeNextStartMs = 0;
    s_timeCycleStartMs = 0;
  }
  markIrrigationSyncDirty();
  if (!saveIrrigationConfigToNvs()) {
    s_server.send(500, "application/json", "{\"ok\":0,\"error\":\"config_persist_failed\"}");
    return;
  }

  char body[320] = {0};
  (void)snprintf(
      body,
      sizeof(body),
      "{\"ok\":1,\"mode\":\"%s\",\"manualActive\":%s,\"manualDurationSec\":%u,\"autoStartPermille\":%u,\"autoStopPermille\":%u,\"timeIntervalMin\":%u,\"timeRunDurationSec\":%u}",
      irrigationModeToText(s_irrigationMode),
      s_manualIrrigationActive ? "true" : "false",
      static_cast<unsigned>(s_manualDurationSec),
      static_cast<unsigned>(s_autoStartPermille),
      static_cast<unsigned>(s_autoStopPermille),
      static_cast<unsigned>(s_timeIntervalMin),
      static_cast<unsigned>(s_timeRunDurationSec));
  s_server.send(200, "application/json", body);
}

static void onIrrigationManualStartApi()
{
  if (s_manualIrrigationActive) {
    Serial.println("OBS: manual irrigation start ignored: already active");
    s_server.send(200, "application/json", "{\"ok\":1,\"manualActive\":true}");
    return;
  }

  const ControlAvailabilitySnapshot controlSnapshot = computeControlAvailabilitySnapshot();
  const bool controlOffline = strcmp(controlSnapshot.manualBlockedReason, "control_offline") == 0;
  const bool controlReachableNow = hasControlReachablePresence(millis());
  if (!controlOffline && strcmp(controlSnapshot.manualBlockedReason, "none") != 0) {
    Serial.print("OBS: manual irrigation start rejected: ");
    Serial.println(controlSnapshot.manualBlockedReason);
    char body[160] = {0};
    (void)snprintf(
        body,
        sizeof(body),
        "{\"ok\":0,\"error\":\"manual_start_blocked\",\"reason\":\"%s\",\"controlStatus\":\"%s\"}",
        controlSnapshot.manualBlockedReason,
        controlSnapshot.status);
    s_server.send(409, "application/json", body);
    return;
  }

  uint16_t requestedDurationSec = s_manualDurationSec;
  if (s_server.hasArg("durationSec") && !parseUint16Arg("durationSec", &requestedDurationSec)) {
    s_server.send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_durationSec\"}");
    return;
  }
  requestedDurationSec = clampU16(requestedDurationSec, MANUAL_DURATION_MIN_SEC, MANUAL_DURATION_MAX_SEC);
  s_manualDurationSec = requestedDurationSec;
  if (!saveIrrigationConfigToNvs()) {
    Serial.println("OBS: warning, manual duration not persisted");
  }

  startIrrigation(millis(), requestedDurationSec, "MANUAL start API");
  bool sentNow = false;
  if (controlOffline || !controlReachableNow) {
    s_controlPhase = CONTROL_CMD_PENDING_START;
    s_pendingControlCmd.active = false;
    Serial.println("OBS: manual irrigation start scheduled (control unreachable/sleeping)");
  } else {
    s_controlPhase = CONTROL_CMD_PENDING_START;
    beginPendingControlCommand(REMOTE_BUTTON_IRRIGATION_START, millis());
    sentNow = sendDesiredIrrigationState();
    if (sentNow) {
      s_lastIrrigationSyncMs = millis();
      s_irrigationSyncDirty = false;
    }
  }

  Serial.println("OBS: manual irrigation start accepted");
  if (controlOffline) {
    s_server.send(
        200,
        "application/json",
        "{\"ok\":1,\"manualActive\":false,\"syncPending\":1,\"pendingCommand\":0,\"scheduled\":1}");
  } else {
    s_server.send(
        200,
        "application/json",
        sentNow
          ? "{\"ok\":1,\"manualActive\":false,\"syncPending\":0,\"pendingCommand\":1,\"scheduled\":0}"
          : "{\"ok\":1,\"manualActive\":false,\"syncPending\":1,\"pendingCommand\":1,\"scheduled\":0}");
  }
}

static void onIrrigationManualStopApi()
{
  if (!s_manualIrrigationActive) {
    Serial.println("OBS: manual irrigation stop ignored: already stopped");
    s_server.send(200, "application/json", "{\"ok\":1,\"manualActive\":false}");
    return;
  }

  stopIrrigation("manual stop API");
  s_controlPhase = CONTROL_CMD_PENDING_STOP;
  beginPendingControlCommand(REMOTE_BUTTON_IRRIGATION_STOP, millis());
  const bool sentNow = sendDesiredIrrigationState();
  if (sentNow) {
    s_lastIrrigationSyncMs = millis();
    s_irrigationSyncDirty = false;
  }

  Serial.println("OBS: manual irrigation stopped");
  s_server.send(
      200,
      "application/json",
      sentNow
        ? "{\"ok\":1,\"manualActive\":false,\"syncPending\":0,\"pendingCommand\":1}"
        : "{\"ok\":1,\"manualActive\":false,\"syncPending\":1,\"pendingCommand\":1}");
}

static void onNodesApi()
{
  char body[4096] = {0};
  (void)composeNodesJson(body, sizeof(body), millis());
  s_server.send(200, "application/json", body);
}

static void onSystemSummaryApi()
{
  char body[384] = {0};
  (void)composeSystemSummaryJson(body, sizeof(body), millis());

  s_server.send(200, "application/json", body);
}

}  // namespace

void headObservabilityInit()
{
  s_sensorLabelsPrefsReady = s_sensorLabelsPrefs.begin(SENSOR_LABELS_NVS_NAMESPACE, false);
  s_irrigationPrefsReady = s_irrigationPrefs.begin(IRRIGATION_CONFIG_NVS_NAMESPACE, false);
  loadSensorLabelsFromNvs();
  loadIrrigationConfigFromNvs();
  loadIrrigationLeaseIdFromNvs();

  s_server.on("/api/nodes", HTTP_GET, onNodesApi);
  s_server.on("/api/system/summary", HTTP_GET, onSystemSummaryApi);
  s_server.on("/api/irrigation/config", HTTP_GET, onIrrigationConfigGetApi);
  s_server.on("/api/irrigation/config", HTTP_POST, onIrrigationConfigPostApi);
  s_server.on("/api/irrigation/manual/start", HTTP_POST, onIrrigationManualStartApi);
  s_server.on("/api/irrigation/manual/stop", HTTP_POST, onIrrigationManualStopApi);
  s_server.on("/api/events", HTTP_GET, onEventsSseApi);
  s_server.on("/api/track/export.csv", HTTP_GET, onTrackExportCsvApi);
  s_server.on("/api/sensors/rename", HTTP_POST, onSensorRenameApi);
  s_server.on("/api/sensors/unpair", HTTP_POST, onSensorUnpairApi);
  s_server.on("/api/sensors/calibrate", HTTP_POST, onSensorCalibrateApi);
  headProvisioningInit(&s_server);
  s_server.begin();
  Serial.println("OBS: HTTP /api/nodes + web console ready");
}

void headObservabilityTick()
{
  const uint32_t nowMs = millis();
  consumeControlCommandAcks(nowMs);
  pendingControlCommandTick(nowMs);

  const bool presenceOverrideAllowed =
      s_lastControlAckAtMs == 0 ||
      static_cast<uint32_t>(nowMs - s_lastControlAckAtMs) >= CONTROL_ACK_PRESENCE_GRACE_MS;

  if (!s_pendingControlCmd.active && presenceOverrideAllowed && hasControlOnlinePresence()) {
    TelemetryHeadNodePresence nodes[8] = {};
    const uint8_t count = telemetryHeadGetPresence(nodes, 8);
    for (uint8_t i = 0; i < count; ++i) {
      if (!nodes[i].isControl) {
        continue;
      }
      if (nodes[i].irrigationActive) {
        if (s_requestedRunDurationSec > 0 && s_manualRunDeadlineMs == 0) {
          s_manualRunDeadlineMs = nowMs + (s_requestedRunDurationSec * 1000UL);
          Serial.print("OBS: run deadline restored from telemetry presence sec=");
          Serial.println((unsigned long)s_requestedRunDurationSec);
        }
        s_controlConfirmedIrrigationActive = true;
        s_controlPhase = CONTROL_CMD_ACTIVE;
      } else if (s_controlPhase != CONTROL_CMD_PENDING_START && s_controlPhase != CONTROL_CMD_PENDING_STOP) {
        s_controlConfirmedIrrigationActive = false;
        if (s_controlPhase != CONTROL_CMD_LOST) {
          s_controlPhase = CONTROL_CMD_IDLE;
        }
      }
      break;
    }
  }

  s_server.handleClient();
  irrigationAutomationTick(nowMs);
  reconcileControlToDesiredState(nowMs);
  irrigationSyncTick(nowMs);
  headProvisioningTick(nowMs);
  ssePushSnapshotIfChanged(nowMs);
}

bool headObservabilityRequestIrrigationSync()
{
  const bool sent = sendDesiredIrrigationState();
  if (sent) {
    s_lastIrrigationSyncMs = millis();
    s_irrigationSyncDirty = false;
  }
  return sent;
}

#else

void headObservabilityInit() {}
void headObservabilityTick() {}
bool headObservabilityRequestIrrigationSync() { return false; }

#endif
