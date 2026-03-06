#include <Arduino.h>
#include "esp_now_helpers.h"
#include "common_config.h"
#include <WiFi.h>
#include "pairing.h"
#include "telemetry.h"
#include "head_observability.h"
#include "head_wifi_provisioning.h"
#include "leds.h"
#include "button.h"
#include "app_log.h"


#if defined(DEVICE_ROLE_HEAD)

static const uint32_t MULTIPRESS_WINDOW_MS = 1400;

struct HeadPressEvents {
  bool single;
  bool triple;
  bool debug;
};

static uint8_t s_headPressCount = 0;
static uint32_t s_headPressWindowDeadlineMs = 0;

static void resetHeadMultipress()
{
  s_headPressCount = 0;
  s_headPressWindowDeadlineMs = 0;
}

static HeadPressEvents processHeadMultipress(bool shortPress, uint32_t now)
{
  HeadPressEvents events{false, false, false};

  if (shortPress) {
    if (s_headPressCount < 255) {
      s_headPressCount++;
    }
    s_headPressWindowDeadlineMs = now + MULTIPRESS_WINDOW_MS;
  }

  if (s_headPressCount == 0) {
    return events;
  }
  if ((int32_t)(now - s_headPressWindowDeadlineMs) < 0) {
    return events;
  }

  if (s_headPressCount >= 5) {
    events.debug = true;
  } else if (s_headPressCount == 3) {
    events.triple = true;
  } else if (s_headPressCount == 1) {
    events.single = true;
  }

  s_headPressCount = 0;
  return events;
}


/*
  Receive handler.
*/
static void onRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (pairingOnRecv(src_mac, data, len)) {
    return;
  }
  telemetryOnRecv(src_mac, data, len);
}

/*
  Send status handler (driver-level).
  Not used for acceptance, but useful for debugging.
*/
static void onSend(const uint8_t* dst_mac, bool success)
{
    (void)dst_mac;
    (void)success;
}



void setup() {
  Serial.begin(115200);

#if defined(HEAD_WAIT_FOR_SERIAL_DEV)
  unsigned long start = millis();
  while (!Serial && (millis() - start < 8000)) {
    delay(10);
  }
#endif

  Serial.println();
    Serial.println("HEAD: Pairing 2.0 always-open");

    Serial.println("HEAD: using factory STA MAC");

  if (!espnowInit(ESPNOW_CHANNEL, onRecv, onSend)) {
      Serial.println("espnowInit() failed");
      while (true) { delay(1000); }
  }

    pairingInitHead(1);
    pairingHeadSetOpen(false);
    logStartupCommon("HEAD", true, pairingHeadHasPairedNode());
    telemetryInit();
    headObservabilityInit();
    ledsInit(LED_DEFAULT_CONFIG.pin, LED_DEFAULT_CONFIG.activeHigh);
    buttonInit(BUTTON_HEAD_CONFIG.pin, BUTTON_HEAD_CONFIG.activeLow, BUTTON_HEAD_CONFIG.usePullup);
    ledsSetBaseMode(LED_MODE_IDLE);


  Serial.println("ESP-NOW ready.");
  Serial.println("USED MAC: " + WiFi.macAddress());

}

void loop() {
    const uint32_t now = millis();
    buttonTick(now);
  const bool rawShortPress = buttonConsumeShortPress();
    const HeadPressEvents pressEvents = processHeadMultipress(rawShortPress, now);

    static bool lastOpenState = false;
    pairingHeadTick(now);

    if (buttonConsumeLongPress()) {
      Serial.println("PAIRING(HEAD): factory reset requested");
      pairingHeadFactoryReset();
      telemetryHeadClearPresence();
      ledsTriggerOnce(LED_MODE_FACTORY_RESET_ONCE);
    }

    if (pressEvents.triple) {
      if (pairingHeadIsOpen()) {
        Serial.println("PAIRING(HEAD): factory reset requested by triple press");
        pairingHeadFactoryReset();
        telemetryHeadClearPresence();
        ledsTriggerOnce(LED_MODE_SUCCESS_ONCE);
      } else {
        Serial.println("PAIRING(HEAD): triple press ignored (pairing window closed)");
        ledsTriggerOnce(LED_MODE_ERROR_ONCE);
      }
    }

    if (pressEvents.single) {
      if (pairingHeadIsOpen()) {
        pairingHeadSetOpen(false);
        resetHeadMultipress();
        Serial.println("PAIRING(HEAD): pairing window closed by user");
        ledsTriggerOnce(LED_MODE_ERROR_ONCE);
      } else {
        pairingHeadSetOpen(true);
        Serial.println("PAIRING(HEAD): pairing window opened");
        ledsSetBaseMode(LED_MODE_PAIRING_OPEN);
      }
    }

    if (pressEvents.debug) {
      buttonEnableDebug();
    }
    if (pressEvents.debug || buttonConsumeDebugEnabledEvent()) {
      Serial.println("DEBUG gate: enabled for this boot");
      ledsTriggerOnce(LED_MODE_DEBUG_CONFIRM);
    }

    if (pairingHeadConsumePairSuccessEvent()) {
      const bool openNow = pairingHeadIsOpen();
      if (openNow) {
        ledsSetBaseMode(LED_MODE_PAIRING_OPEN);
      } else {
        ledsSetBaseMode(LED_MODE_OFF);
      }
      ledsTriggerOnce(LED_MODE_SUCCESS_ONCE);
      Serial.println("PAIRING(HEAD): pair success indication");
    }

    const bool isOpen = pairingHeadIsOpen();
    if (isOpen && !lastOpenState) {
      ledsSetBaseMode(LED_MODE_PAIRING_OPEN);
    } else if (!isOpen && lastOpenState) {
      ledsSetBaseMode(LED_MODE_OFF);
    }
    lastOpenState = isOpen;

    ledsTick(now);
    telemetryTickHead(now);
    headObservabilityTick();
    pairingTick();
    delay(10);
}

#endif
