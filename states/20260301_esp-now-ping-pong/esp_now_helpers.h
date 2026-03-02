#pragma once
#include <stdint.h>
#include <stddef.h>


/*
  Simple PING/PONG test messages for Milestone 1.
*/
#pragma pack(push, 1)
struct PingMsg {
    uint8_t  ver;
    uint8_t  type;   // 1 = PING
    uint16_t seq;
};

struct PongMsg {
    uint8_t  ver;
    uint8_t  type;   // 2 = PONG
    uint16_t seq;
};
#pragma pack(pop)


/*
  Callback signature for received ESP-NOW frames.
*/
typedef void (*EspNowRecvCb)(const uint8_t* src_mac,
                             const uint8_t* data,
                             int len);

/*
  Callback signature for send status.
*/
typedef void (*EspNowSendCb)(const uint8_t* dst_mac,
                             bool success);

/*
  Initialize ESP-NOW layer.
*/
bool espnowInit(uint8_t channel,
                const uint8_t* custom_sta_mac,
                EspNowRecvCb recv_cb,
                EspNowSendCb send_cb);

bool espnowAddPeer(const uint8_t peer_mac[6],
                   uint8_t channel,
                   bool encrypt = false);

bool espnowSend(const uint8_t dst_mac[6],
                const uint8_t* data,
                size_t len);

void macToString(const uint8_t mac[6],
                 char* out,
                 size_t out_len);