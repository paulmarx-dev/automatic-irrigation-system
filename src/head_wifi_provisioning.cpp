#include "head_wifi_provisioning.h"

#if defined(DEVICE_ROLE_HEAD)

#include <Arduino.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_wifi.h>

#include "common_config.h"

namespace {

static constexpr const char* NVS_NS_WIFI_CFG = "wifi_cfg";
static constexpr const char* KEY_VER = "ver";
static constexpr const char* KEY_CONFIGURED = "configured";
static constexpr const char* KEY_SSID = "ssid";
static constexpr const char* KEY_PASS = "pass";

static constexpr uint8_t WIFI_CFG_VER = 1;
static constexpr uint8_t WIFI_CONFIGURED_TRUE = 1;

static constexpr uint32_t AP_WINDOW_MS = 120000;
static constexpr uint32_t PROVISIONING_SESSION_MS = 600000;
static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 25000;

struct WifiCredentials {
  bool configured;
  String ssid;
  String pass;
};

static WebServer* s_server = nullptr;
static bool s_fsMounted = false;

static bool s_sessionActive = false;
static bool s_apActive = false;
static bool s_staConnecting = false;
static bool s_staConnected = false;
static bool s_setupStarted = false;

static uint32_t s_sessionDeadlineMs = 0;
static uint32_t s_staConnectDeadlineMs = 0;

static String s_apSsid;
static String s_lastError;

static WifiCredentials s_credentials{false, String(), String()};

static void restoreEspNowChannel()
{
  WiFi.mode(WIFI_STA);
  const esp_err_t err = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (err != ESP_OK) {
    Serial.print("PROV: restore ESPNOW channel failed err=");
    Serial.println(static_cast<int>(err));
  }
}

static bool nvsLoadCredentials(WifiCredentials* outCreds)
{
  if (!outCreds) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(NVS_NS_WIFI_CFG, true)) {
    return false;
  }

  const uint8_t ver = prefs.getUChar(KEY_VER, 0);
  const uint8_t configured = prefs.getUChar(KEY_CONFIGURED, 0);
  if (ver != WIFI_CFG_VER || configured != WIFI_CONFIGURED_TRUE) {
    prefs.end();
    outCreds->configured = false;
    outCreds->ssid = String();
    outCreds->pass = String();
    return false;
  }

  outCreds->ssid = prefs.getString(KEY_SSID, String());
  outCreds->pass = prefs.getString(KEY_PASS, String());
  prefs.end();

  outCreds->configured = outCreds->ssid.length() > 0;
  if (!outCreds->configured) {
    outCreds->pass = String();
  }
  return outCreds->configured;
}

static bool nvsSaveCredentials(const String& ssid, const String& pass)
{
  if (ssid.length() == 0) {
    return false;
  }

  if (s_credentials.configured && s_credentials.ssid == ssid && s_credentials.pass == pass) {
    return true;
  }

  Preferences prefs;
  if (!prefs.begin(NVS_NS_WIFI_CFG, false)) {
    return false;
  }

  const bool verOk = prefs.putUChar(KEY_VER, WIFI_CFG_VER) == 1;
  const bool cfgOk = prefs.putUChar(KEY_CONFIGURED, WIFI_CONFIGURED_TRUE) == 1;
  const bool ssidOk = prefs.putString(KEY_SSID, ssid) == ssid.length();
  const bool passOk = prefs.putString(KEY_PASS, pass) == pass.length();
  prefs.end();

  if (verOk && cfgOk && ssidOk && passOk) {
    s_credentials.configured = true;
    s_credentials.ssid = ssid;
    s_credentials.pass = pass;
  }

  return verOk && cfgOk && ssidOk && passOk;
}

static bool nvsClearCredentials()
{
  Preferences prefs;
  if (!prefs.begin(NVS_NS_WIFI_CFG, false)) {
    return false;
  }

  const bool verOk = prefs.putUChar(KEY_VER, WIFI_CFG_VER) == 1;
  const bool cfgOk = prefs.putUChar(KEY_CONFIGURED, 0) == 1;
  const bool hadSsid = prefs.isKey(KEY_SSID);
  const bool ssidOk = !hadSsid || prefs.remove(KEY_SSID);
  const bool hadPass = prefs.isKey(KEY_PASS);
  const bool passOk = !hadPass || prefs.remove(KEY_PASS);
  prefs.end();

  if (verOk && cfgOk && ssidOk && passOk) {
    s_credentials.configured = false;
    s_credentials.ssid = String();
    s_credentials.pass = String();
    return true;
  }

  return false;
}

static void ensureApStarted()
{
  if (s_apActive) {
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  if (WiFi.softAP(s_apSsid.c_str(), nullptr, ESPNOW_CHANNEL, false, 1)) {
    s_apActive = true;
    Serial.print("PROV: AP started ssid=");
    Serial.println(s_apSsid);
    Serial.print("PROV: AP IP=");
    Serial.println(WiFi.softAPIP());
  } else {
    s_lastError = "AP_START_FAILED";
    Serial.println("PROV: AP start failed");
  }
}

static void stopAp()
{
  if (!s_apActive) {
    return;
  }

  WiFi.softAPdisconnect(true);
  s_apActive = false;
  Serial.println("PROV: AP stopped");
}

static void beginStaConnect(uint32_t nowMs)
{
  if (!s_credentials.configured) {
    s_lastError = "NO_CREDENTIALS";
    return;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(s_credentials.ssid.c_str(), s_credentials.pass.c_str());
  s_staConnecting = true;
  s_staConnected = false;
  s_setupStarted = true;
  s_staConnectDeadlineMs = nowMs + STA_CONNECT_TIMEOUT_MS;
  s_lastError = "";

  Serial.print("PROV: STA connect start ssid=");
  Serial.println(s_credentials.ssid);
}

static const char* wlStatusToText(wl_status_t status)
{
  switch (status) {
    case WL_CONNECTED: return "CONNECTED";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    case WL_IDLE_STATUS: return "IDLE";
    default: return "UNKNOWN";
  }
}

static void serveFile(const char* path, const char* contentType)
{
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

static void onProvisioningStatusApi()
{
  wl_status_t wlStatus = WiFi.status();
  const uint32_t nowMs = millis();
  const uint32_t remainingMs = (s_sessionActive && (int32_t)(s_sessionDeadlineMs - nowMs) > 0)
      ? (s_sessionDeadlineMs - nowMs)
      : 0;

  String body;
  body.reserve(512);
  body += "{";
  body += "\"sessionActive\":";
  body += s_sessionActive ? "true" : "false";
  body += ",\"apActive\":";
  body += s_apActive ? "true" : "false";
  body += ",\"staConnecting\":";
  body += s_staConnecting ? "true" : "false";
  body += ",\"staConnected\":";
  body += s_staConnected ? "true" : "false";
  body += ",\"staStatus\":\"";
  body += wlStatusToText(wlStatus);
  body += "\"";
  body += ",\"staIp\":\"";
  body += WiFi.localIP().toString();
  body += "\"";
  body += ",\"apSsid\":\"";
  body += s_apSsid;
  body += "\"";
  body += ",\"sessionRemainingSec\":";
  body += String(remainingMs / 1000);
  body += ",\"lastError\":\"";
  body += s_lastError;
  body += "\"";
  body += "}";

  s_server->send(200, "application/json", body);
}

static void onProvisioningConfigApi()
{
  if (!s_server->hasArg("ssid")) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"missing-ssid\"}");
    return;
  }

  const String ssid = s_server->arg("ssid");
  const String pass = s_server->hasArg("password") ? s_server->arg("password") : String();

  if (ssid.length() == 0) {
    s_server->send(400, "application/json", "{\"ok\":false,\"error\":\"empty-ssid\"}");
    return;
  }

  if (!nvsSaveCredentials(ssid, pass)) {
    s_lastError = "NVS_SAVE_FAILED";
    s_server->send(500, "application/json", "{\"ok\":false,\"error\":\"save-failed\"}");
    return;
  }

  const uint32_t nowMs = millis();
  s_sessionActive = true;
  s_sessionDeadlineMs = nowMs + PROVISIONING_SESSION_MS;
  s_setupStarted = true;
  ensureApStarted();
  beginStaConnect(nowMs);

  s_server->send(200, "application/json", "{\"ok\":true}");
}

static void onProvisioningResetApi()
{
  if (!nvsClearCredentials()) {
    s_lastError = "NVS_CLEAR_FAILED";
    s_server->send(500, "application/json", "{\"ok\":false,\"error\":\"clear-failed\"}");
    return;
  }

  WiFi.disconnect(false, false);
  s_staConnecting = false;
  s_staConnected = false;
  s_setupStarted = false;
  s_lastError = "RESET_BY_API";

  const uint32_t nowMs = millis();
  s_sessionActive = true;
  s_sessionDeadlineMs = nowMs + AP_WINDOW_MS;
  ensureApStarted();

  s_server->send(200, "application/json", "{\"ok\":true}");
}

static void registerRoutes()
{
  s_server->on("/", HTTP_GET, []() { serveFile("/provisioning/index.html", "text/html"); });
  s_server->on("/provisioning", HTTP_GET, []() { serveFile("/provisioning/index.html", "text/html"); });
  s_server->on("/provisioning/index.html", HTTP_GET, []() { serveFile("/provisioning/index.html", "text/html"); });
  s_server->on("/provisioning/app.js", HTTP_GET, []() { serveFile("/provisioning/app.js", "application/javascript"); });
  s_server->on("/provisioning/style.css", HTTP_GET, []() { serveFile("/provisioning/style.css", "text/css"); });

  s_server->on("/api/provisioning/status", HTTP_GET, onProvisioningStatusApi);
  s_server->on("/api/provisioning/config", HTTP_POST, onProvisioningConfigApi);
  s_server->on("/api/provisioning/reset", HTTP_POST, onProvisioningResetApi);
}

}  // namespace

void headProvisioningInit(WebServer* server)
{
  s_server = server;
  s_fsMounted = SPIFFS.begin(false);
  if (!s_fsMounted) {
    Serial.println("PROV: SPIFFS mount failed");
  }

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  char ssid[32] = {0};
  (void)snprintf(ssid, sizeof(ssid), "Irrigation-Setup-%02X%02X", mac[4], mac[5]);
  s_apSsid = ssid;

  nvsLoadCredentials(&s_credentials);
  registerRoutes();

  Serial.print("PROV: credentials configured=");
  Serial.println(s_credentials.configured ? "yes" : "no");
}

void headProvisioningTick(uint32_t nowMs)
{
  if (s_staConnecting) {
    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      const IPAddress connectedIp = WiFi.localIP();
      s_staConnecting = false;
      s_staConnected = true;
      s_lastError = "";
      Serial.print("PROV: STA connected IP=");
      Serial.println(connectedIp);

      if (s_apActive) {
        stopAp();
      }

      WiFi.disconnect(false, false);
      restoreEspNowChannel();
      Serial.println("PROV: STA test complete, restored ESPNOW channel");

      s_sessionActive = false;
      s_setupStarted = false;
    } else if ((int32_t)(nowMs - s_staConnectDeadlineMs) >= 0) {
      s_staConnecting = false;
      s_staConnected = false;
      s_lastError = wlStatusToText(status);
      Serial.print("PROV: STA connect timeout status=");
      Serial.println(s_lastError);
      WiFi.disconnect(false, false);
      ensureApStarted();
    }
  }

  if (s_sessionActive && (int32_t)(nowMs - s_sessionDeadlineMs) >= 0) {
    s_sessionActive = false;
    s_staConnecting = false;
    s_setupStarted = false;
    if (s_apActive) {
      stopAp();
    }
    Serial.println("PROV: session timeout, closing AP");
  }
}

void headProvisioningOpenSession(uint32_t nowMs)
{
  s_sessionActive = true;
  s_setupStarted = false;
  s_sessionDeadlineMs = nowMs + AP_WINDOW_MS;
  ensureApStarted();
}

void headProvisioningCloseSession()
{
  s_sessionActive = false;
  s_staConnecting = false;
  s_setupStarted = false;
  stopAp();
  WiFi.disconnect(false, false);
  restoreEspNowChannel();
}

bool headProvisioningIsSetupStarted()
{
  return s_setupStarted;
}

bool headProvisioningHandleTriplePressReset(uint32_t nowMs, bool pairingWindowOpen)
{
  if (!pairingWindowOpen && !s_sessionActive) {
    return false;
  }

  if (!nvsClearCredentials()) {
    s_lastError = "NVS_CLEAR_FAILED";
    return false;
  }

  WiFi.disconnect(false, false);
  s_staConnecting = false;
  s_staConnected = false;
  s_setupStarted = false;
  s_lastError = "RESET_BY_BUTTON";

  s_sessionActive = true;
  s_sessionDeadlineMs = nowMs + AP_WINDOW_MS;
  ensureApStarted();

  Serial.println("PROV: Wi-Fi credentials cleared by triple press");
  return true;
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

void headProvisioningOpenSession(uint32_t nowMs)
{
  (void)nowMs;
}

void headProvisioningCloseSession() {}

bool headProvisioningIsSetupStarted()
{
  return false;
}

bool headProvisioningHandleTriplePressReset(uint32_t nowMs, bool pairingWindowOpen)
{
  (void)nowMs;
  (void)pairingWindowOpen;
  return false;
}

#endif
