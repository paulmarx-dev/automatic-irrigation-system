#pragma once

#include <stdint.h>

#include "common_config.h"

enum LedMode : uint8_t {
  LED_MODE_OFF = 0,
  LED_MODE_BOOT = 1,
  LED_MODE_IDLE = 2,
  LED_MODE_DEBUG_CONFIRM = 3,
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
