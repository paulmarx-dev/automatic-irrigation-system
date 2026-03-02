#pragma once

#include <Arduino.h>

#ifndef LED_PIN
  #define LED_PIN 8
#endif

#ifndef LED_ACTIVE_HIGH
  #if defined(DEVICE_ROLE_SENSOR)
    #define LED_ACTIVE_HIGH 0
  #else
    #define LED_ACTIVE_HIGH 1
  #endif
#endif

#ifndef HAS_RGB_LED
  #if defined(DEVICE_ROLE_HEAD)
    #define HAS_RGB_LED 1
  #else
    #define HAS_RGB_LED 0
  #endif
#endif

#ifndef RGB_LED_BRIGHTNESS
  #define RGB_LED_BRIGHTNESS 32
#endif

static const uint8_t SYS_ID = 0x10;

/*
  Test MACs for Milestone 1.
  MAC scheme: 02:SYS:TYPE:HEAD:NODEH:NODEL
*/
static const uint8_t MAC_HEAD[6]    = {0x02, 0x10, 0x01, 0x01, 0x00, 0x01};
static const uint8_t MAC_SENSOR1[6] = {0x02, 0x10, 0x03, 0x01, 0x00, 0x01};
static const uint8_t MAC_CONTROL1[6]= {0x02, 0x10, 0x02, 0x01, 0x00, 0x01};

/*
  Fixed ESPNOW channel for the whole system.
*/
static constexpr uint8_t ESPNOW_CHANNEL = 6;
static constexpr uint32_t PAIRING_HEAD_OPEN_MS = 120000;
static constexpr uint32_t PAIRING_NODE_JOIN_MS = 60000;

/*
  Protocol version (will be used later in all messages).
*/
static constexpr uint8_t PROTOCOL_VERSION = 1;

// *************************************************************************************
// MAC generation functions
// *************************************************************************************
enum NodeType : uint8_t {
  TYPE_HEAD    = 0x01,
  TYPE_CONTROL = 0x02,
  TYPE_SENSOR  = 0x03
};

static inline void makeMac(uint8_t out[6], NodeType type, uint8_t headId, uint16_t nodeId) {
  out[0] = 0x02;
  out[1] = SYS_ID;
  out[2] = static_cast<uint8_t>(type);
  out[3] = headId;
  out[4] = static_cast<uint8_t>(nodeId >> 8);
  out[5] = static_cast<uint8_t>(nodeId & 0xFF);
}
