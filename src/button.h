#pragma once

#include <stdint.h>

struct ButtonPinConfig {
  uint8_t pin;
  bool activeLow;
  bool usePullup;
};

static constexpr ButtonPinConfig BUTTON_SENSOR_CONFIG = {9, true, true};
static constexpr ButtonPinConfig BUTTON_HEAD_CONFIG = {9, true, true};
static constexpr ButtonPinConfig BUTTON_CONTROL_CONFIG = {9, true, true};

void buttonInit(uint8_t pin, bool activeLow, bool usePullup);
void buttonTick(uint32_t nowMs);
bool buttonIsDebugEnabled();
bool buttonConsumeDebugEnabledEvent();
