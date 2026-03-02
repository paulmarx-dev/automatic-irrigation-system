#include <Arduino.h>
#include "esp_now_helpers.h"
#include "common_config.h"
#include <WiFi.h>
#include "pairing.h"
#include "telemetry.h"
#include "leds.h"
#include "button.h"


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

  unsigned long start = millis();
  while (!Serial && (millis() - start < 8000)) {
    delay(10);
  }
  
  Serial.println("BOOT");

  Serial.println();
    Serial.println("HEAD: Pairing 2.0 always-open");

    printMac("HEAD custom MAC: ", MAC_HEAD);

  if (!espnowInit(ESPNOW_CHANNEL, MAC_HEAD, onRecv, onSend)) {
      Serial.println("espnowInit() failed");
      while (true) { delay(1000); }
  }

    pairingInitHead(1);
    telemetryInit();
    ledsInit(LED_HEAD_CONFIG.pin, LED_HEAD_CONFIG.activeHigh);
    buttonInit(BUTTON_HEAD_CONFIG.pin, BUTTON_HEAD_CONFIG.activeLow, BUTTON_HEAD_CONFIG.usePullup);
    ledsSetMode(LED_MODE_IDLE);


  Serial.println("ESP-NOW ready.");
  Serial.println("USED MAC: " + WiFi.macAddress());

}

void loop() {
    const uint32_t now = millis();
    buttonTick(now);
    if (buttonConsumeDebugEnabledEvent()) {
      Serial.println("DEBUG gate: enabled for this boot");
      ledsSetMode(LED_MODE_DEBUG_CONFIRM);
    }
    ledsTick(now);
    pairingTick();
    delay(10);
}

#endif
