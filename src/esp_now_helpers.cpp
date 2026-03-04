/*
  ESP-NOW helper layer.

  Supported targets:
  - ESP32-C3
  - ESP32-C6
  - ESP32-S3

  Requires ESP-IDF >= 5 (Arduino Core 3.x).
*/
#include "esp_now_helpers.h"

#include <Arduino.h>
#include <WiFi.h>

#include <esp_wifi.h>
#include <esp_now.h>




// *************************************************************************************
// ESP-NOW helper implementation
// *************************************************************************************

static EspNowRecvCb s_recv_cb = nullptr;
static EspNowSendCb s_send_cb = nullptr;

/*
  ESP-NOW send callback (IDF 5.x API).
*/
static void onEspNowSend(const esp_now_send_info_t* info,
                         esp_now_send_status_t status)
{
    if (!s_send_cb || !info || !info->des_addr) {
        return;
    }

    const bool ok = (status == ESP_NOW_SEND_SUCCESS);

    s_send_cb(info->des_addr, ok);
}

/*
  ESP-NOW receive callback (IDF 5.x API).
  Used on ESP32-C3, ESP32-C6 and ESP32-S3.
*/
static void onEspNowRecv(const esp_now_recv_info_t* info,
                         const uint8_t* data,
                         int len)
{
    if (!s_recv_cb || !info || !info->src_addr || !data || len <= 0) {
        return;
    }

    s_recv_cb(info->src_addr, data, len);
}


void macToString(const uint8_t mac[6], char* out, size_t out_len)
{
    if (!out || out_len < 18) {
        return;
    }

    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}


static bool setStaChannel(uint8_t channel)
{
    /*
      ESP-NOW works on the currently configured WiFi channel.
      We set it explicitly to keep a stable RF environment.
    */
    esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    return (err == ESP_OK);
}

static bool s_initialized = false;

bool espnowInit(uint8_t channel, 
                EspNowRecvCb recv_cb,
                EspNowSendCb send_cb)
{
    if (s_initialized) { return false; }
    if (channel < 1 || channel > 13) { return false; }

    s_recv_cb = recv_cb;
    s_send_cb = send_cb;

    /*
      1) Put WiFi into STA mode (required for ESP-NOW).
            2) Use factory STA MAC.
            3) Set fixed channel before peer operations.
    */
    WiFi.mode(WIFI_STA);
    //WiFi.disconnect(true, true);
    Serial.println("REAL MAC: " + WiFi.macAddress());

    // Ensure WiFi driver is started
    // if (esp_wifi_start() != ESP_OK) { return false; }

    if (!setStaChannel(channel)) { return false; }

    if (esp_now_init() != ESP_OK) { return false; }

    /*
      Register callbacks.
    */
    if (esp_now_register_send_cb(onEspNowSend) != ESP_OK) { return false; }

    if (esp_now_register_recv_cb(onEspNowRecv) != ESP_OK) { return false; }

    /*
      Optional: set PMK if you plan encryption later.
      For now we keep it open.
    */
    // esp_now_set_pmk((const uint8_t*)"pmk1234567890123");

    s_initialized = true;
    return true;
}

bool espnowAddPeer(const uint8_t peer_mac[6], uint8_t channel, bool encrypt)
{
    if (!peer_mac) {
        return false;
    }

    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, peer_mac, 6);
    peer.channel = channel;
    peer.encrypt = encrypt;

    /*
      Add peer idempotently:
      - If exists, delete and add again (simplest safe approach).
    */
    if (esp_now_is_peer_exist(peer_mac)) {
        esp_now_del_peer(peer_mac);
    }

    esp_err_t err = esp_now_add_peer(&peer);
    return (err == ESP_OK);
}

bool espnowIsPeer(const uint8_t peer_mac[6])
{
    if (!peer_mac) {
        return false;
    }

    return esp_now_is_peer_exist(peer_mac);
}

bool espnowEnsurePeer(const uint8_t peer_mac[6], uint8_t channel, bool encrypt)
{
    if (!peer_mac) {
        return false;
    }

    if (espnowIsPeer(peer_mac)) {
        return true;
    }

    return espnowAddPeer(peer_mac, channel, encrypt);
}

bool espnowRemovePeer(const uint8_t peer_mac[6])
{
    if (!peer_mac) {
        return false;
    }

    if (!espnowIsPeer(peer_mac)) {
        return true;
    }

    return (esp_now_del_peer(peer_mac) == ESP_OK);
}

bool espnowSend(const uint8_t dst_mac[6], const uint8_t* data, size_t len)
{
    if (!dst_mac || !data || len == 0) {
        return false;
    }

    esp_err_t err = esp_now_send(dst_mac, data, (int)len);
    return (err == ESP_OK);
}
