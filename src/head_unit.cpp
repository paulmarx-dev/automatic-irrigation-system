#include <Arduino.h>
#include "esp_now_helpers.h"
#include "common_config.h"
#include <WiFi.h>
#include "pairing.h"
#include "telemetry.h"
#include "leds.h"
#include "button.h"
#include "app_log.h"


#if defined(DEVICE_ROLE_HEAD)


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

static void printMac(const char* label, const uint8_t mac[6])
{
    char buf[18] = {0};
    macToString(mac, buf, sizeof(buf));
    Serial.print(label);
    Serial.println(buf);
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

    printMac("HEAD custom MAC: ", MAC_HEAD);

  if (!espnowInit(ESPNOW_CHANNEL, MAC_HEAD, onRecv, onSend)) {
      Serial.println("espnowInit() failed");
      while (true) { delay(1000); }
  }

    pairingInitHead(1);
    pairingHeadSetOpen(false);
    logStartupCommon("HEAD", true, pairingHeadHasPairedNode());
    telemetryInit();
    ledsInit(LED_DEFAULT_CONFIG.pin, LED_DEFAULT_CONFIG.activeHigh);
    buttonInit(BUTTON_HEAD_CONFIG.pin, BUTTON_HEAD_CONFIG.activeLow, BUTTON_HEAD_CONFIG.usePullup);
    ledsSetBaseMode(LED_MODE_IDLE);


  Serial.println("ESP-NOW ready.");
  Serial.println("USED MAC: " + WiFi.macAddress());

}

void loop() {
    const uint32_t now = millis();
    buttonTick(now);

    static bool lastOpenState = false;
    pairingHeadTick(now);

    if (buttonConsumeLongPress()) {
      Serial.println("PAIRING(HEAD): factory reset requested");
      pairingHeadFactoryReset();
      ledsTriggerOnce(LED_MODE_FACTORY_RESET_ONCE);
    }

    if (buttonConsumeShortPress()) {
      if (pairingHeadIsOpen()) {
        pairingHeadSetOpen(false);
        Serial.println("PAIRING(HEAD): pairing window closed by user");
        ledsTriggerOnce(LED_MODE_ERROR_ONCE);
      } else {
        pairingHeadSetOpen(true);
        Serial.println("PAIRING(HEAD): pairing window opened");
        ledsSetBaseMode(LED_MODE_PAIRING_OPEN);
      }
    }

    if (buttonConsumeDebugEnabledEvent()) {
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
      ledsTriggerOnce(LED_MODE_SUCCESS_DOUBLE);
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
    pairingTick();
    delay(10);
}

#endif
