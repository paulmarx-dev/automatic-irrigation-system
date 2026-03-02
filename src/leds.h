#pragma once

#include <stdint.h>

#include "common_config.h"

enum LedMode : uint8_t {
  LED_MODE_OFF = 0,
  LED_MODE_BOOT = 1,
  LED_MODE_IDLE = 2,
  LED_MODE_DEBUG_CONFIRM = 3,
  LED_MODE_PAIRING_OPEN = 4,
  LED_MODE_JOINING = 5,
  LED_MODE_SUCCESS_ONCE = 6,
  LED_MODE_SUCCESS_DOUBLE = 7,
  LED_MODE_ERROR_REPEAT = 8,
  LED_MODE_ERROR_ONCE = 9,
  LED_MODE_FACTORY_RESET_ONCE = 10,
  LED_MODE_CAL_ENTER_ONCE = 11,
  LED_MODE_CAL_MEASURE_DRY = 12,
  LED_MODE_CAL_PROMPT_WET = 13,
  LED_MODE_CAL_MEASURE_WET = 14,
  LED_MODE_CAL_DONE_ONCE = 15,
  LED_MODE_CAL_ERROR_REPEAT = 16,
};

struct LedPinConfig {
  uint8_t pin;
  bool activeHigh;
};

static constexpr LedPinConfig LED_DEFAULT_CONFIG = {
  static_cast<uint8_t>(LED_PIN),
  LED_ACTIVE_HIGH != 0
};

void ledsInit(uint8_t pin, bool activeHigh);
void ledsSetMode(LedMode mode);
void ledsPulseOnce(uint16_t onMs);
void ledsTick(uint32_t nowMs);
