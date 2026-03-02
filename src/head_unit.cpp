#include <Arduino.h>
#include "esp_now_helpers.h"
#include "common_config.h"
#include <WiFi.h>
#include "pairing.h"
#include "telemetry.h"


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


  Serial.println("ESP-NOW ready.");
  Serial.println("USED MAC: " + WiFi.macAddress());

}

void loop() {
    pairingTick();
    delay(10);
}

#endif
