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

namespace {

static WebServer s_server(80);
static constexpr uint8_t MAX_SENSOR_LABELS = 8;
static constexpr size_t SENSOR_NAME_MAX = 32;
static constexpr uint16_t SENSOR_LABELS_NVS_VERSION = 1;
static const char* SENSOR_LABELS_NVS_NAMESPACE = "sensor_labels";
static const char* SENSOR_LABELS_NVS_KEY = "labels_blob";
static constexpr uint16_t IRRIGATION_CONFIG_NVS_VERSION = 1;
static const char* IRRIGATION_CONFIG_NVS_NAMESPACE = "irrigation_cfg";
static const char* IRRIGATION_CONFIG_NVS_KEY = "config_blob";

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
};

struct IrrigationConfigNvsBlob {
  uint16_t version;
  uint8_t mode;
  uint8_t reserved;
};

static IrrigationMode s_irrigationMode = IRRIGATION_MODE_AUTO;
static bool s_manualIrrigationActive = false;

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

static const char* irrigationModeToText(IrrigationMode mode)
{
  switch (mode) {
    case IRRIGATION_MODE_AUTO:
      return "AUTO";
    case IRRIGATION_MODE_MANUAL:
      return "MANUAL";
    case IRRIGATION_MODE_OFF:
      return "OFF";
    default:
      return "AUTO";
  }
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
  return false;
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
  const size_t written = s_irrigationPrefs.putBytes(IRRIGATION_CONFIG_NVS_KEY, &blob, sizeof(blob));
  return written == sizeof(blob);
}

static void loadIrrigationConfigFromNvs()
{
  s_irrigationMode = IRRIGATION_MODE_AUTO;
  if (!s_irrigationPrefsReady) {
    return;
  }

  if (!s_irrigationPrefs.isKey(IRRIGATION_CONFIG_NVS_KEY)) {
    return;
  }

  IrrigationConfigNvsBlob blob{};
  const size_t read = s_irrigationPrefs.getBytes(IRRIGATION_CONFIG_NVS_KEY, &blob, sizeof(blob));
  if (read != sizeof(blob) || blob.version != IRRIGATION_CONFIG_NVS_VERSION || blob.mode > IRRIGATION_MODE_OFF) {
    return;
  }

  s_irrigationMode = static_cast<IrrigationMode>(blob.mode);
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
  char body[128] = {0};
  (void)snprintf(
      body,
      sizeof(body),
      "{\"mode\":\"%s\",\"manualActive\":%s}",
      irrigationModeToText(s_irrigationMode),
      s_manualIrrigationActive ? "true" : "false");
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

  s_irrigationMode = requestedMode;
  if (s_irrigationMode != IRRIGATION_MODE_MANUAL) {
    s_manualIrrigationActive = false;
  }
  if (!saveIrrigationConfigToNvs()) {
    s_server.send(500, "application/json", "{\"ok\":0,\"error\":\"config_persist_failed\"}");
    return;
  }

  char body[128] = {0};
  (void)snprintf(
      body,
      sizeof(body),
      "{\"ok\":1,\"mode\":\"%s\",\"manualActive\":%s}",
      irrigationModeToText(s_irrigationMode),
      s_manualIrrigationActive ? "true" : "false");
  s_server.send(200, "application/json", body);
}

static void onIrrigationManualStartApi()
{
  if (s_irrigationMode != IRRIGATION_MODE_MANUAL) {
    Serial.println("OBS: manual irrigation start rejected: mode not MANUAL");
    s_server.send(409, "application/json", "{\"ok\":0,\"error\":\"mode_not_manual\"}");
    return;
  }

  if (s_manualIrrigationActive) {
    Serial.println("OBS: manual irrigation start ignored: already active");
    s_server.send(200, "application/json", "{\"ok\":1,\"manualActive\":true}");
    return;
  }

  s_manualIrrigationActive = true;
  Serial.println("OBS: manual irrigation started");
  s_server.send(200, "application/json", "{\"ok\":1,\"manualActive\":true}");
}

static void onIrrigationManualStopApi()
{
  if (!s_manualIrrigationActive) {
    Serial.println("OBS: manual irrigation stop ignored: already stopped");
    s_server.send(200, "application/json", "{\"ok\":1,\"manualActive\":false}");
    return;
  }

  s_manualIrrigationActive = false;
  Serial.println("OBS: manual irrigation stopped");
  s_server.send(200, "application/json", "{\"ok\":1,\"manualActive\":false}");
}

static void onNodesApi()
{
  TelemetryHeadNodePresence nodes[8] = {};
  const uint8_t count = telemetryHeadGetPresence(nodes, 8);
  const uint32_t nowMs = millis();

  char body[4096] = {0};
  size_t offset = 0;

  offset += static_cast<size_t>(snprintf(body + offset, sizeof(body) - offset, "["));

  for (uint8_t i = 0; i < count; ++i) {
    const TelemetryHeadNodePresence& node = nodes[i];
    char mac[18] = {0};
    char name[SENSOR_NAME_MAX] = {0};
    macToString(node.mac, mac, sizeof(mac));
    resolveSensorName(node.nodeId, name);
    const uint32_t lastSeenSecAgo = static_cast<uint32_t>(nowMs - node.lastSeenMs) / 1000;

    offset += static_cast<size_t>(snprintf(
        body + offset,
        sizeof(body) - offset,
      "%s{\"slot\":%u,\"mac\":\"%s\",\"name\":\"%s\",\"nodeId\":%u,\"state\":\"%s\",\"moisturePermille\":%u,\"batteryEstMv\":%u,\"batteryState\":\"%s\",\"lastSeenSecAgo\":%lu,\"rxPackets\":%lu,\"rxDuplicates\":%lu,\"rxInvalid\":%lu,\"ackOkSent\":%lu,\"ackNotPairedSent\":%lu}",
        (i == 0) ? "" : ",",
        static_cast<unsigned>(i),
        mac,
        name,
        static_cast<unsigned>(node.nodeId),
        nodeStateToText(node.state),
      static_cast<unsigned>(node.moisturePermille),
      static_cast<unsigned>(node.batteryEstMv),
      batteryStateToText(node.batteryState),
        static_cast<unsigned long>(lastSeenSecAgo),
        static_cast<unsigned long>(node.rxPackets),
        static_cast<unsigned long>(node.rxDuplicates),
        static_cast<unsigned long>(node.rxInvalid),
        static_cast<unsigned long>(node.ackOkSent),
        static_cast<unsigned long>(node.ackNotPairedSent)));

    if (offset >= sizeof(body) - 2) {
      break;
    }
  }

  (void)snprintf(body + offset, sizeof(body) - offset, "]");
  s_server.send(200, "application/json", body);
}

static void onSystemSummaryApi()
{
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

  const uint32_t nowMs = millis();
  const uint32_t pairingRemainingSec = (pairingHeadRemainingMs(nowMs) + 999UL) / 1000UL;
  const uint32_t uptimeSec = nowMs / 1000UL;
  char avgMoisture[16] = "null";
  if (hasMoistureAvg) {
    (void)snprintf(avgMoisture, sizeof(avgMoisture), "%lu", static_cast<unsigned long>(avgMoisturePermille));
  }

  char body[384] = {0};
  (void)snprintf(
      body,
      sizeof(body),
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
      s_manualIrrigationActive ? "true" : "false");

  s_server.send(200, "application/json", body);
}

}  // namespace

void headObservabilityInit()
{
  s_sensorLabelsPrefsReady = s_sensorLabelsPrefs.begin(SENSOR_LABELS_NVS_NAMESPACE, false);
  s_irrigationPrefsReady = s_irrigationPrefs.begin(IRRIGATION_CONFIG_NVS_NAMESPACE, false);
  loadSensorLabelsFromNvs();
  loadIrrigationConfigFromNvs();

  s_server.on("/api/nodes", HTTP_GET, onNodesApi);
  s_server.on("/api/system/summary", HTTP_GET, onSystemSummaryApi);
  s_server.on("/api/irrigation/config", HTTP_GET, onIrrigationConfigGetApi);
  s_server.on("/api/irrigation/config", HTTP_POST, onIrrigationConfigPostApi);
  s_server.on("/api/irrigation/manual/start", HTTP_POST, onIrrigationManualStartApi);
  s_server.on("/api/irrigation/manual/stop", HTTP_POST, onIrrigationManualStopApi);
  s_server.on("/api/sensors/rename", HTTP_POST, onSensorRenameApi);
  s_server.on("/api/sensors/unpair", HTTP_POST, onSensorUnpairApi);
  s_server.on("/api/sensors/calibrate", HTTP_POST, onSensorCalibrateApi);
  headProvisioningInit(&s_server);
  s_server.begin();
  Serial.println("OBS: HTTP /api/nodes + web console ready");
}

void headObservabilityTick()
{
  s_server.handleClient();
  headProvisioningTick(millis());
}

#else

void headObservabilityInit() {}
void headObservabilityTick() {}

#endif
