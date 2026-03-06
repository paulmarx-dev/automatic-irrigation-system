#include "head_observability.h"

#if defined(DEVICE_ROLE_HEAD)

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ctype.h>
#include <stdlib.h>

#include "esp_now_helpers.h"
#include "head_wifi_provisioning.h"
#include "pairing.h"
#include "telemetry.h"

namespace {

static WebServer s_server(80);
static constexpr uint8_t MAX_SENSOR_LABELS = 8;
static constexpr size_t SENSOR_NAME_MAX = 32;

struct SensorLabel {
  bool used;
  uint16_t nodeId;
  char name[SENSOR_NAME_MAX];
};

static SensorLabel s_sensorLabels[MAX_SENSOR_LABELS] = {};

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
  return true;
}

static void clearSensorLabel(uint16_t nodeId)
{
  const int8_t slot = findSensorLabelSlot(nodeId);
  if (slot < 0) {
    return;
  }
  memset(&s_sensorLabels[slot], 0, sizeof(s_sensorLabels[slot]));
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
  const unsigned long parsed = strtoul(nodeIdArg.c_str(), nullptr, 10);
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

}  // namespace

void headObservabilityInit()
{
  s_server.on("/api/nodes", HTTP_GET, onNodesApi);
  s_server.on("/api/sensors/rename", HTTP_POST, onSensorRenameApi);
  s_server.on("/api/sensors/unpair", HTTP_POST, onSensorUnpairApi);
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
