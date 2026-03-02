#include <Arduino.h>
#include "esp_now_helpers.h"
#include "common_config.h"
#include <WiFi.h>



#if defined(DEVICE_ROLE_HEAD)


static constexpr uint8_t MSG_PING = 1;
static constexpr uint8_t MSG_PONG = 2;

static volatile uint16_t g_lastPongSeq = 0;
static volatile bool g_pongReceived = false;

static uint16_t g_seq = 0;
static uint32_t g_sent = 0;
static uint32_t g_ok = 0;
static uint32_t g_missed = 0;

static uint32_t g_pingSentAtMs = 0;


/*
  Receive handler.
*/
static void onRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
    (void)src_mac;

    if (!data || len < (int)sizeof(PongMsg)) {
        return;
    }

    const PongMsg* msg = reinterpret_cast<const PongMsg*>(data);
    if (msg->ver != PROTOCOL_VERSION || msg->type != MSG_PONG) {
        return;
    }

    g_lastPongSeq = msg->seq;
    g_pongReceived = true;
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
  Serial.println("HEAD: Milestone 1 PING/PONG test");

  printMac("HEAD MAC:   ", MAC_HEAD);
  printMac("SENSOR MAC: ", MAC_SENSOR1);

  if (!espnowInit(ESPNOW_CHANNEL, MAC_HEAD, onRecv, onSend)) {
      Serial.println("espnowInit() failed");
      while (true) { delay(1000); }
  }

  if (!espnowAddPeer(MAC_SENSOR1, ESPNOW_CHANNEL, false)) {
      Serial.println("espnowAddPeer(sensor) failed");
      while (true) { delay(1000); }
  }

  Serial.println("ESP-NOW ready.");
  Serial.println("USED MAC: " + WiFi.macAddress());

}



static bool sendPing(uint16_t seq)
{
    PingMsg msg{};
    msg.ver = PROTOCOL_VERSION;
    msg.type = MSG_PING;
    msg.seq = seq;

    const bool ok = espnowSend(MAC_SENSOR1, reinterpret_cast<const uint8_t*>(&msg), sizeof(msg));
    return ok;
}



void loop() {
	/*
      Send a PING, wait for matching PONG, repeat until 100 successes.
      Timeout: 200 ms per attempt.
    */
    if (g_ok >= 100) {
        Serial.println("DONE: 100/100 round-trips OK.");
        while (true) { delay(1000); }
    }

    g_seq++;
    g_pongReceived = false;
    g_lastPongSeq = 0;

    const bool sendOk = sendPing(g_seq);
    g_sent++;

    if (!sendOk) {
        g_missed++;
        Serial.print("PING send failed, seq=");
        Serial.println(g_seq);
        delay(200);
        return;
    }

    g_pingSentAtMs = millis();

    while (millis() - g_pingSentAtMs < 200) {
        if (g_pongReceived && g_lastPongSeq == g_seq) {
            g_ok++;
            const uint32_t rtt = millis() - g_pingSentAtMs;

            Serial.print("OK ");
            Serial.print(g_ok);
            Serial.print("/100, seq=");
            Serial.print(g_seq);
            Serial.print(", rtt_ms=");
            Serial.println(rtt);

            delay(50);
            return;
        }
        delay(1);
    }

    g_missed++;
    Serial.print("MISS, seq=");
    Serial.println(g_seq);

    delay(100);
}

#endif
