#include "head_wifi_provisioning.h"

#if defined(DEVICE_ROLE_HEAD)

#include <Arduino.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "common_config.h"

namespace {

static WebServer* s_server = nullptr;
static bool s_fsMounted = false;
static String s_apSsid;

static void serveFile(const char* path, const char* contentType)
{
  if (!s_server) {
    return;
  }

  if (!s_fsMounted) {
    s_server->send(503, "text/plain", "filesystem-not-mounted");
    return;
  }

  File file = SPIFFS.open(path, FILE_READ);
  if (!file) {
    s_server->send(404, "text/plain", "not-found");
    return;
  }

  s_server->streamFile(file, contentType);
  file.close();
}

static void onWebStatusApi()
{
  String body;
  body.reserve(256);
  body += "{";
  body += "\"mode\":\"AP_ONLY\",";
  body += "\"apSsid\":\"";
  body += s_apSsid;
  body += "\",";
  body += "\"apIp\":\"";
  body += WiFi.softAPIP().toString();
  body += "\"";
  body += "}";

  s_server->send(200, "application/json", body);
}

static void registerRoutes()
{
  s_server->on("/", HTTP_GET, []() { serveFile("/provisioning/index.html", "text/html"); });
  s_server->on("/provisioning", HTTP_GET, []() { serveFile("/provisioning/index.html", "text/html"); });
  s_server->on("/provisioning/index.html", HTTP_GET, []() { serveFile("/provisioning/index.html", "text/html"); });
  s_server->on("/provisioning/app.js", HTTP_GET, []() { serveFile("/provisioning/app.js", "application/javascript"); });
  s_server->on("/provisioning/style.css", HTTP_GET, []() { serveFile("/provisioning/style.css", "text/css"); });
  s_server->on("/api/web/status", HTTP_GET, onWebStatusApi);
}

}  // namespace

void headProvisioningInit(WebServer* server)
{
  s_server = server;
  s_fsMounted = SPIFFS.begin(false);
  if (!s_fsMounted) {
    Serial.println("WEB: SPIFFS mount failed");
  }

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  char ssid[32] = {0};
  (void)snprintf(ssid, sizeof(ssid), "Irrigation-Setup-%02X%02X", mac[4], mac[5]);
  s_apSsid = ssid;

  WiFi.mode(WIFI_AP_STA);
  if (WiFi.softAP(s_apSsid.c_str(), nullptr, ESPNOW_CHANNEL, false, 2)) {
    Serial.print("WEB: AP started ssid=");
    Serial.println(s_apSsid);
    Serial.print("WEB: AP IP=");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("WEB: AP start failed");
  }

  registerRoutes();
}

void headProvisioningTick(uint32_t nowMs)
{
  (void)nowMs;
}

#else

void headProvisioningInit(WebServer* server)
{
  (void)server;
}

void headProvisioningTick(uint32_t nowMs)
{
  (void)nowMs;
}

#endif
