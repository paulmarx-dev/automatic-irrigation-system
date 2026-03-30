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
  Control-unit safety limits.
  - max run cap: hard stop for motor if no fresh authoritative command arrives
  - boot sync window: time budget for CONTROL to ask HEAD for desired irrigation state
*/
static constexpr uint32_t CONTROL_MOTOR_MAX_RUN_CAP_MS = 120000;
static constexpr uint32_t CONTROL_HEAD_SYNC_BOOT_WINDOW_MS = 5000;
static constexpr uint32_t CONTROL_HEAD_SYNC_RETRY_MS = 700;

/*
  Control-unit irrigation battery lockout thresholds for 1S Li-ion/LiPo.
  - STOP_NOW: if battery stays below this while irrigating, force stop
  - BLOCK_START: below this, prevent new irrigation starts
  - RESUME_OK: require recovery above this before allowing starts again
  - CONFIRM_MS: debounce time for entering/leaving lockout states
*/
static constexpr uint16_t CONTROL_BATT_STOP_NOW_MV = 3350;
static constexpr uint16_t CONTROL_BATT_BLOCK_START_MV = 3500;
static constexpr uint16_t CONTROL_BATT_RESUME_OK_MV = 3650;
static constexpr uint32_t CONTROL_BATT_LOCKOUT_CONFIRM_MS = 2500;

/*
  Protocol version (will be used later in all messages).
*/
static constexpr uint8_t PROTOCOL_VERSION = 1;

/*
  Sleep coordination defaults (current iteration scope).
  HEAD issues 60s base sleep with slot+jitter spread.
*/
static constexpr uint32_t SLEEP_BASE_DURATION_MS = 60000;
static constexpr uint8_t SLEEP_SLOT_MAX_UNITS = 9;
static constexpr uint32_t SLEEP_SLOT_WIDTH_MS = 180;
static constexpr uint32_t SLEEP_SLOT_MICRO_JITTER_MS = 30;
static constexpr uint32_t SLEEP_PLAN_VALID_WINDOW_MS = 5000;
static constexpr uint32_t SLEEP_EXPECTED_WAKE_GRACE_MS = 15000;

/*
  Node-side sleep handshake timing.
*/
static constexpr uint32_t WAIT_ACK_ACK_TIMEOUT_MS = 1200;
static constexpr uint8_t SLEEP_ACK_RETRY_MAX = 3;
static constexpr uint32_t SLEEP_ACK_RETRY_MIN_MS = 150;
static constexpr uint32_t SLEEP_ACK_RETRY_JITTER_MS = 200;

/*
  Node sleep mode defaults.
  0=off, 1=light sleep, 5=deep sleep.
*/
static constexpr uint8_t SENSOR_SLEEP_MODE_DEFAULT = 5;
static constexpr uint8_t CONTROL_SLEEP_MODE_DEFAULT = 5;
