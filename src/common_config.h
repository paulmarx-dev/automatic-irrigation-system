#pragma once

#include <Arduino.h>

#ifndef LED_PIN
  #define LED_PIN 8
#endif

#ifndef LED_ACTIVE_HIGH
  #if defined(DEVICE_ROLE_SENSOR) || defined(DEVICE_ROLE_CONTROL)
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

/*
  Fixed ESPNOW channel for the whole system.
*/
static constexpr uint8_t ESPNOW_CHANNEL = 6;
static constexpr uint32_t PAIRING_HEAD_OPEN_MS = 120000;
static constexpr uint32_t PAIRING_NODE_JOIN_MS = 60000;

/*
  Head-side battery status thresholds (estimated battery voltage in mV).
  <= NEEDS_REPLACEMENT: hard replacement warning
  <= CRITICAL: low battery warning
  >  CRITICAL: OK
*/
static constexpr uint16_t BATTERY_NEEDS_REPLACEMENT_MV = 3200;
static constexpr uint16_t BATTERY_CRITICAL_MV = 3500;

/*
  Telemetry scheduling defaults (scalable up to 8 sensors).
  - base interval: nominal period per node
  - jitter: random spread to avoid repeated collisions
  - phase spread: deterministic initial offset by nodeId slot
*/
static constexpr uint8_t TELEMETRY_SCHEDULE_MAX_NODES = 8;
static constexpr uint32_t TELEMETRY_BASE_INTERVAL_MS = 5000;
static constexpr uint32_t TELEMETRY_INTERVAL_JITTER_MS = 1500;
static constexpr uint32_t TELEMETRY_PHASE_SPREAD_MS = 2400;
static constexpr uint32_t TELEMETRY_FIRST_SEND_MIN_DELAY_MS = 200;
static constexpr uint32_t TELEMETRY_FIRST_SEND_JITTER_MS = 800;

/*
  Protocol version (will be used later in all messages).
*/
static constexpr uint8_t PROTOCOL_VERSION = 1;
