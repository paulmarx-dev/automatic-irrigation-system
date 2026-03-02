#include <Arduino.h>
#include "esp_now_helpers.h"
#include "common_config.h"
#include <WiFi.h>
#include <string.h>
#include "pairing.h"
#include "protocol.h"


#if defined(DEVICE_ROLE_HEAD)


/*
  Receive handler.
*/
static void onRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (pairingOnRecv(src_mac, data, len)) {
    return;
  }

  if (!src_mac || !data || len != (int)sizeof(MsgTelemetry)) {
    return;
  }

  const MsgTelemetry* telemetry = reinterpret_cast<const MsgTelemetry*>(data);
  if (telemetry->hdr.ver != PROTO_VER || telemetry->hdr.type != MSG_TELEMETRY) {
    return;
  }

  uint8_t pairedMac[6] = {0};
  if (!pairingHeadPairedNodeMac(pairedMac)) {
    return;
  }

  if (memcmp(src_mac, pairedMac, 6) != 0) {
    return;
  }

  Serial.print("TELEMETRY nodeId=");
  Serial.print((unsigned long)telemetry->hdr.nodeId);
  Serial.print(" seq=");
  Serial.print((unsigned long)telemetry->hdr.seq);
  Serial.print(" moisturePermille=");
  Serial.print((unsigned long)telemetry->moisturePermille);
  Serial.print(" moistureRawMv=");
  Serial.print((unsigned long)telemetry->moistureRawMv);
  Serial.print(" batteryRawMv=");
  Serial.print((unsigned long)telemetry->batteryRawMv);
  Serial.print(" batteryEstMv=");
  Serial.print((unsigned long)telemetry->batteryEstMv);
  Serial.print(" flags=");
  Serial.println((unsigned long)telemetry->flags);
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


  Serial.println("ESP-NOW ready.");
  Serial.println("USED MAC: " + WiFi.macAddress());

}

void loop() {
    pairingTick();
    delay(10);
}

#endif
