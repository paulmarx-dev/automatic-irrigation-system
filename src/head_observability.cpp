#include "head_observability.h"

#if defined(DEVICE_ROLE_HEAD)

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "esp_now_helpers.h"
#include "head_wifi_provisioning.h"
#include "telemetry.h"

namespace {

static WebServer s_server(80);

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
    macToString(node.mac, mac, sizeof(mac));
    const uint32_t lastSeenSecAgo = static_cast<uint32_t>(nowMs - node.lastSeenMs) / 1000;

    offset += static_cast<size_t>(snprintf(
        body + offset,
        sizeof(body) - offset,
      "%s{\"slot\":%u,\"mac\":\"%s\",\"nodeId\":%u,\"state\":\"%s\",\"moisturePermille\":%u,\"batteryEstMv\":%u,\"batteryState\":\"%s\",\"lastSeenSecAgo\":%lu,\"rxPackets\":%lu,\"rxDuplicates\":%lu,\"rxInvalid\":%lu,\"ackOkSent\":%lu,\"ackNotPairedSent\":%lu}",
        (i == 0) ? "" : ",",
        static_cast<unsigned>(i),
        mac,
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
