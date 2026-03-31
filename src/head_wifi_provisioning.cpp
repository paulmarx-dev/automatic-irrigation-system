#include "head_wifi_provisioning.h"

#if defined(DEVICE_ROLE_HEAD)

#include <Arduino.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "common_config.h"
#include "pairing.h"
#include "telemetry.h"

namespace {

static WebServer* s_server = nullptr;
static bool s_fsMounted = false;
static String s_apSsid;
static String s_unitName;
static String s_apPassword;
static Preferences s_unitPrefs;
static bool s_unitPrefsReady = false;
static bool s_apReconfigurePending = false;
static uint32_t s_apReconfigureAtMs = 0;
static String s_pendingApSsid;
static String s_pendingApPassword;

static constexpr uint16_t UNIT_CONFIG_NVS_VERSION = 1;
static constexpr size_t UNIT_NAME_MAX = 32;
static constexpr size_t AP_PASSWORD_MAX = 64;
static const char* UNIT_CONFIG_NVS_NAMESPACE = "unit_cfg";
static const char* UNIT_CONFIG_NVS_KEY = "unit_blob";

struct UnitConfigNvsBlob {
  uint16_t version;
  char unitName[UNIT_NAME_MAX];
  char apPassword[AP_PASSWORD_MAX];
};

static String makeFactorySsid()
{
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  char ssid[32] = {0};
  (void)snprintf(ssid, sizeof(ssid), "Irrigation-Setup-%02X%02X", mac[4], mac[5]);
  return String(ssid);
}

static bool sanitizeUnitName(const String& input, char outName[UNIT_NAME_MAX])
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
  for (size_t i = 0; i < raw.length() && outLen < (UNIT_NAME_MAX - 1); ++i) {
    const char ch = raw[i];
    if (isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '_' || ch == '-') {
      outName[outLen++] = ch;
    }
  }

  outName[outLen] = '\0';
  return outLen > 0;
}

static bool sanitizeApPassword(const String& input, char outPassword[AP_PASSWORD_MAX])
{
  if (!outPassword) {
    return false;
  }

  String raw = input;
  raw.trim();
  if (raw.length() == 0) {
    outPassword[0] = '\0';
    return true;
  }
  if (raw.length() < 8 || raw.length() > 63) {
    return false;
  }

  strlcpy(outPassword, raw.c_str(), AP_PASSWORD_MAX);
  return true;
}

static bool saveUnitConfigToNvs()
{
  if (!s_unitPrefsReady) {
    return false;
  }

  UnitConfigNvsBlob blob{};
  blob.version = UNIT_CONFIG_NVS_VERSION;
  strlcpy(blob.unitName, s_unitName.c_str(), sizeof(blob.unitName));
  strlcpy(blob.apPassword, s_apPassword.c_str(), sizeof(blob.apPassword));

  const size_t written = s_unitPrefs.putBytes(UNIT_CONFIG_NVS_KEY, &blob, sizeof(blob));
  return written == sizeof(blob);
}

static void loadUnitConfigFromNvs()
{
  if (!s_unitPrefsReady || !s_unitPrefs.isKey(UNIT_CONFIG_NVS_KEY)) {
    return;
  }

  UnitConfigNvsBlob blob{};
  const size_t read = s_unitPrefs.getBytes(UNIT_CONFIG_NVS_KEY, &blob, sizeof(blob));
  if (read != sizeof(blob) || blob.version != UNIT_CONFIG_NVS_VERSION) {
    return;
  }

  if (blob.unitName[0] != '\0') {
    s_unitName = blob.unitName;
  }
  s_apPassword = blob.apPassword;
}

static bool startSoftAp(const String& ssid, const String& password)
{
  const char* passwordPtr = password.length() > 0 ? password.c_str() : nullptr;
  return WiFi.softAP(ssid.c_str(), passwordPtr, ESPNOW_CHANNEL, false, 2);
}

static void scheduleApReconfigure(const String& ssid, const String& password)
{
  s_pendingApSsid = ssid;
  s_pendingApPassword = password;
  s_apReconfigurePending = true;
  s_apReconfigureAtMs = millis() + 1200;
}

static bool resetUnitConfigToFactory()
{
  s_unitName = makeFactorySsid();
  s_apSsid = s_unitName;
  s_apPassword = "";

  if (s_unitPrefsReady) {
    (void)s_unitPrefs.remove(UNIT_CONFIG_NVS_KEY);
  }

  scheduleApReconfigure(s_apSsid, s_apPassword);
  return true;
}

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

static size_t provisioningComposeWebStatusJson(char* body, size_t bodySize, uint32_t nowMs)
{
  if (!body || bodySize == 0) {
    return 0;
  }

  const uint32_t remainingMs = pairingHeadRemainingMs(nowMs);
  const uint32_t remainingSec = (remainingMs + 999UL) / 1000UL;
  const String apIp = WiFi.softAPIP().toString();
  const int written = snprintf(
      body,
      bodySize,
      "{\"mode\":\"AP_ONLY\",\"apSsid\":\"%s\",\"apIp\":\"%s\",\"pairingOpen\":%s,\"pairingRemainingSec\":%lu}",
      s_apSsid.c_str(),
      apIp.c_str(),
      pairingHeadIsOpen() ? "true" : "false",
      static_cast<unsigned long>(remainingSec));
  if (written <= 0) {
    body[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written < static_cast<int>(bodySize) ? written : static_cast<int>(bodySize - 1));
}

static size_t provisioningComposeUnitStatusJson(char* body, size_t bodySize, uint32_t nowMs)
{
  if (!body || bodySize == 0) {
    return 0;
  }

  const uint32_t uptimeSec = nowMs / 1000UL;
  const int written = snprintf(
      body,
      bodySize,
      "{\"unitName\":\"%s\",\"apSsid\":\"%s\",\"apPasswordSet\":%s,\"firmwareVersion\":\"%s %s\",\"uptimeSec\":%lu}",
      s_unitName.c_str(),
      s_apSsid.c_str(),
      s_apPassword.length() > 0 ? "true" : "false",
      __DATE__,
      __TIME__,
      static_cast<unsigned long>(uptimeSec));
  if (written <= 0) {
    body[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written < static_cast<int>(bodySize) ? written : static_cast<int>(bodySize - 1));
}

static void onWebStatusApi()
{
  char body[320] = {0};
  (void)provisioningComposeWebStatusJson(body, sizeof(body), millis());
  s_server->send(200, "application/json", body);
}

static void onUnitStatusApi()
{
  char body[320] = {0};
  (void)provisioningComposeUnitStatusJson(body, sizeof(body), millis());
  s_server->send(200, "application/json", body);
}

static void onUnitRenameApi()
{
  if (!s_server->hasArg("name")) {
    s_server->send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_args\"}");
    return;
  }

  char sanitizedName[UNIT_NAME_MAX] = {0};
  if (!sanitizeUnitName(s_server->arg("name"), sanitizedName)) {
    s_server->send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_name\"}");
    return;
  }

  s_unitName = sanitizedName;
  s_apSsid = sanitizedName;
  if (!saveUnitConfigToNvs()) {
    s_server->send(500, "application/json", "{\"ok\":0,\"error\":\"persist_failed\"}");
    return;
  }

  scheduleApReconfigure(s_apSsid, s_apPassword);
  s_server->send(200, "application/json", "{\"ok\":1,\"reconnect\":1}");
}

static void onUnitPasswordApi()
{
  if (!s_server->hasArg("password")) {
    s_server->send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_args\"}");
    return;
  }

  char sanitizedPassword[AP_PASSWORD_MAX] = {0};
  if (!sanitizeApPassword(s_server->arg("password"), sanitizedPassword)) {
    s_server->send(400, "application/json", "{\"ok\":0,\"error\":\"invalid_password\"}");
    return;
  }

  s_apPassword = sanitizedPassword;
  if (!saveUnitConfigToNvs()) {
    s_server->send(500, "application/json", "{\"ok\":0,\"error\":\"persist_failed\"}");
    return;
  }

  scheduleApReconfigure(s_apSsid, s_apPassword);
  s_server->send(200, "application/json", "{\"ok\":1,\"reconnect\":1}");
}

static void onUnitFactoryResetApi()
{
  if (!s_server->hasArg("confirm") || s_server->arg("confirm") != "RESET") {
    s_server->send(400, "application/json", "{\"ok\":0,\"error\":\"confirm_required\"}");
    return;
  }

  pairingHeadFactoryReset();
  telemetryHeadClearPresence();
  headProvisioningFactoryReset();
  s_server->send(200, "application/json", "{\"ok\":1,\"reconnect\":1,\"factoryReset\":1}");
}

static void onPairingOpenApi()
{
  pairingHeadSetOpen(true);
  const uint32_t remainingSec = (pairingHeadRemainingMs(millis()) + 999UL) / 1000UL;
  String body;
  body.reserve(96);
  body += "{\"ok\":1,\"pairingOpen\":true,\"pairingRemainingSec\":";
  body += String(static_cast<unsigned long>(remainingSec));
  body += "}";
  s_server->send(200, "application/json", body);
}

static void onPairingCloseApi()
{
  pairingHeadSetOpen(false);
  s_server->send(200, "application/json", "{\"ok\":1,\"pairingOpen\":false,\"pairingRemainingSec\":0}");
}

static void registerRoutes()
{
  s_server->on("/", HTTP_GET, []() { serveFile("/provisioning/index.html", "text/html"); });
  s_server->on("/provisioning", HTTP_GET, []() { serveFile("/provisioning/index.html", "text/html"); });
  s_server->on("/provisioning/index.html", HTTP_GET, []() { serveFile("/provisioning/index.html", "text/html"); });
  s_server->on("/provisioning/app.js", HTTP_GET, []() { serveFile("/provisioning/app.js", "application/javascript"); });
  s_server->on("/provisioning/chart.js", HTTP_GET, []() { serveFile("/provisioning/chart.js", "application/javascript"); });
  s_server->on("/provisioning/style.css", HTTP_GET, []() { serveFile("/provisioning/style.css", "text/css"); });
  s_server->on("/api/web/status", HTTP_GET, onWebStatusApi);
  s_server->on("/api/pairing/open", HTTP_POST, onPairingOpenApi);
  s_server->on("/api/pairing/close", HTTP_POST, onPairingCloseApi);
  s_server->on("/api/unit/status", HTTP_GET, onUnitStatusApi);
  s_server->on("/api/unit/rename", HTTP_POST, onUnitRenameApi);
  s_server->on("/api/unit/password", HTTP_POST, onUnitPasswordApi);
  s_server->on("/api/unit/factory-reset", HTTP_POST, onUnitFactoryResetApi);
}

}  // namespace

size_t headProvisioningComposeWebStatusJson(char* body, size_t bodySize, uint32_t nowMs)
{
  return provisioningComposeWebStatusJson(body, bodySize, nowMs);
}

size_t headProvisioningComposeUnitStatusJson(char* body, size_t bodySize, uint32_t nowMs)
{
  return provisioningComposeUnitStatusJson(body, bodySize, nowMs);
}

void headProvisioningInit(WebServer* server)
{
  s_server = server;
  s_fsMounted = SPIFFS.begin(false);
  if (!s_fsMounted) {
    Serial.println("WEB: SPIFFS mount failed");
  }

  s_unitName = makeFactorySsid();

  s_unitPrefsReady = s_unitPrefs.begin(UNIT_CONFIG_NVS_NAMESPACE, false);
  loadUnitConfigFromNvs();
  s_apSsid = s_unitName;

  WiFi.mode(WIFI_AP_STA);
  if (startSoftAp(s_apSsid, s_apPassword)) {
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
  if (!s_apReconfigurePending) {
    return;
  }

  if ((int32_t)(nowMs - s_apReconfigureAtMs) < 0) {
    return;
  }

  s_apReconfigurePending = false;
  WiFi.softAPdisconnect(true);
  if (startSoftAp(s_pendingApSsid, s_pendingApPassword)) {
    s_apSsid = s_pendingApSsid;
    s_apPassword = s_pendingApPassword;
    Serial.print("WEB: AP reconfigured ssid=");
    Serial.println(s_apSsid);
  } else {
    Serial.println("WEB: AP reconfigure failed");
  }
}

void headProvisioningFactoryReset()
{
  if (resetUnitConfigToFactory()) {
    Serial.println("WEB: unit config reset to factory defaults");
  } else {
    Serial.println("WEB: unit config factory reset failed");
  }
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

size_t headProvisioningComposeWebStatusJson(char* body, size_t bodySize, uint32_t nowMs)
{
  (void)nowMs;
  if (!body || bodySize == 0) {
    return 0;
  }
  body[0] = '\0';
  return 0;
}

size_t headProvisioningComposeUnitStatusJson(char* body, size_t bodySize, uint32_t nowMs)
{
  (void)nowMs;
  if (!body || bodySize == 0) {
    return 0;
  }
  body[0] = '\0';
  return 0;
}

void headProvisioningFactoryReset()
{
}

#endif
