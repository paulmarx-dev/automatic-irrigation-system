#include <Arduino.h>
#include <WiFi.h>
#include "esp_now_helpers.h"
#include "pairing.h"
#include "pairing_nvs.h"
#include "telemetry.h"
#include "leds.h"
#include "button.h"
#include "app_log.h"

#if defined(DEVICE_ROLE_CONTROL)

static const char *DEVICE_ROLE = "CONTROL";
static const char *DEVICE_ID = "2";
static bool s_autoJoinTriggered = false;

static void onRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (pairingOnRecv(src_mac, data, len)) {
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
  if (pairingNvsLoadNode(restoredHeadMac, &restoredNodeId)) {
    pairingNodeRestorePairedHead(restoredHeadMac, restoredNodeId);
    (void)espnowEnsurePeer(restoredHeadMac, ESPNOW_CHANNEL, false);
    s_autoJoinTriggered = true;
    Serial.printf("PAIRING(CONTROL): restored pair from NVS nodeId=%u\n", (unsigned)restoredNodeId);
  }

  logStartupCommon("CONTROL", true, pairingNodeIsPaired());
  telemetryInit();

  ledsInit(LED_DEFAULT_CONFIG.pin, LED_DEFAULT_CONFIG.activeHigh);
  buttonInit(BUTTON_CONTROL_CONFIG.pin, BUTTON_CONTROL_CONFIG.activeLow, BUTTON_CONTROL_CONFIG.usePullup);
  ledsSetBaseMode(LED_MODE_IDLE);

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
    Serial.println("PAIRING(CONTROL): auto-join on boot");
    s_autoJoinTriggered = true;
  }

  static bool pairStateInitialized = false;
  static bool wasPaired = false;
  static bool lastJoinModeActive = false;

  const bool isPaired = pairingNodeIsPaired();
  bool joinModeActive = pairingNodeIsInJoinMode();
  const bool shortPress = buttonConsumeShortPress();
  const bool longPress = buttonConsumeLongPress();

  if (!pairStateInitialized) {
    wasPaired = isPaired;
    pairStateInitialized = true;
  }

  if (longPress) {
    Serial.println("PAIRING(CONTROL): factory reset requested");
    pairingNodeFactoryReset();
    if (!pairingNvsClearNode()) {
      Serial.println("PAIRING(CONTROL): NVS clear failed");
    }
    pairingNodeEnterJoinMode(now);
    s_autoJoinTriggered = true;
    joinModeActive = true;
    ledsTriggerOnce(LED_MODE_FACTORY_RESET_ONCE);
  }

  if (!wasPaired && isPaired) {
    uint8_t headMac[6] = {0};
    const uint16_t nodeId = pairingNodeId();
    if (pairingNodeHeadMac(headMac)) {
      if (!pairingNvsSaveNode(headMac, nodeId)) {
        Serial.println("PAIRING(CONTROL): NVS save failed");
      }
    }
    Serial.println("PAIRING(CONTROL): join success");
    ledsTriggerOnce(LED_MODE_SUCCESS_DOUBLE);
  }
  wasPaired = isPaired;

  if (!isPaired && joinModeActive && pairingNodeJoinExpired(now)) {
    pairingNodeExitJoinMode();
    joinModeActive = false;
    Serial.println("PAIRING(CONTROL): join window expired");
    ledsTriggerOnce(LED_MODE_ERROR_ONCE);
  }

  if (shortPress) {
    if (isPaired) {
      Serial.println("PAIRING(CONTROL): short press ignored (already paired)");
    } else if (pairingNodeIsInJoinMode()) {
      pairingNodeExitJoinMode();
      joinModeActive = false;
      Serial.println("PAIRING(CONTROL): join window canceled by user");
      ledsTriggerOnce(LED_MODE_ERROR_ONCE);
    } else {
      pairingNodeEnterJoinMode(now);
      joinModeActive = true;
      Serial.println("PAIRING(CONTROL): join window opened");
      ledsSetBaseMode(LED_MODE_JOINING);
    }
  }

  if (buttonConsumeDebugEnabledEvent()) {
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
