#include <Arduino.h>
#include <stdint.h>
#include <esp_system.h>

// *************************************************************************************
// 1. general message types and pairing functions for ESP-NOW communication
// *************************************************************************************

// #pragma once

/*
 Device Registry Record
*/
// struct NodeRecord
// {
//   uint16_t nodeId;
//   uint64_t uid;
//   uint8_t mac[6];
//   DeviceRole type;
//   uint32_t lastSeen;
// };
// usage 
// Registry.add(uid, mac, nodeId)

/*
 Device roles
*/
enum DeviceRole : uint8_t {
    ROLE_HEAD    = 1,
    ROLE_SENSOR  = 2,
    ROLE_CONTROL = 3
};

/*
 Message types
*/
enum MessageType : uint8_t {
    MSG_BEACON   = 1,
    MSG_JOIN_REQ = 2,
    MSG_OFFER    = 3,
    MSG_CONFIRM  = 4,
    MSG_ACK      = 5,
    MSG_FORGET   = 6
};

/*
 Common message header
*/
struct MsgHeader {
    uint8_t  version;
    uint8_t  type;
    uint16_t seq;
};

// *************************************************************************************
// 2. Beacon message (Head → broadcast)
// *************************************************************************************

/*
 Head beacon message for discovery and pairing
*/
struct MsgBeacon {
    MsgHeader hdr;
    uint8_t headId;
    uint8_t pairingOpen;
    uint32_t nonce;
};


// *************************************************************************************
// 3. Join request (Node → broadcast)
// *************************************************************************************

/*
 Node join request message
*/
struct MsgJoinRequest {
    MsgHeader hdr;
    uint64_t deviceUID;
    uint8_t role;
    uint32_t nonce;
};


// *************************************************************************************
// 4. Offer message (Head → Node)
// *************************************************************************************
/*
 Pairing offer from head
*/
struct MsgOffer {
    MsgHeader hdr;
    uint64_t deviceUID;
    uint16_t nodeId;
    uint8_t headMAC[6];
    uint8_t channel;
    uint32_t nonce;
};


// *************************************************************************************
// 5. Confirm message (Node → Head)
// *************************************************************************************
/*
 Node confirms pairing
*/
struct MsgConfirm {
    MsgHeader hdr;
    uint64_t deviceUID;
    uint16_t nodeId;
    uint32_t nonce;
};


// *************************************************************************************
// 6. ACK message (Head → Node)
// *************************************************************************************
/*
 Generic acknowledgement message
*/
struct MsgAck {
    MsgHeader hdr;
    uint8_t success;
};


// *************************************************************************************
// 7. Get unique ID of ESP32 - we use factory MAC as UID
// *************************************************************************************
/*
 Returns unique device identifier derived from factory MAC
*/
uint64_t getDeviceUID()
{
    uint64_t mac = ESP.getEfuseMac();
    return mac;
}


// *************************************************************************************
// 8. Basic pairing state machine
// *************************************************************************************
/*
 Pairing states
*/
enum PairState {
    PAIR_IDLE,
    PAIR_JOIN_MODE,
    PAIR_WAIT_OFFER,
    PAIR_WAIT_ACK,
    PAIR_PAIRED
};


// *************************************************************************************
// 9. Configuration of the device (NVS comes later)
// *************************************************************************************
/*
 Persistent device configuration
*/
struct DeviceConfig {
    bool paired;
    uint8_t headMAC[6];
    uint16_t nodeId;
    uint8_t role;
};