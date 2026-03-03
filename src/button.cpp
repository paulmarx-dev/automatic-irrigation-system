#include "button.h"

#include <Arduino.h>

static const uint16_t BOOT_WINDOW_MS = 5000;
static const uint8_t REQUIRED_PRESSES = 5;
static const uint16_t DEBOUNCE_MS = 30;
static const uint16_t LONG_PRESS_MS = 3000;

static uint8_t s_pin = 255;
static bool s_activeLow = true;

static bool s_lastRawPressed = false;
static bool s_stablePressed = false;
static uint32_t s_lastRawChangeMs = 0;

static uint32_t s_bootWindowEndMs = 0;
static uint8_t s_pressCount = 0;
static bool s_debugEnabled = false;
static bool s_debugEventPending = false;
static bool s_shortPressPending = false;
static bool s_longPressPending = false;
static uint32_t s_pressStartedMs = 0;
static bool s_longPressFired = false;

static bool readPressedRaw()
{
  if (s_pin == 255) {
    return false;
  }
  const int value = digitalRead(s_pin);
  return s_activeLow ? (value == LOW) : (value == HIGH);
}

void buttonInit(uint8_t pin, bool activeLow, bool usePullup)
{
  s_pin = pin;
  s_activeLow = activeLow;

  if (usePullup) {
    pinMode(s_pin, INPUT_PULLUP);
  } else {
    pinMode(s_pin, INPUT);
  }

  const uint32_t nowMs = millis();
  s_lastRawPressed = readPressedRaw();
  s_stablePressed = s_lastRawPressed;
  s_lastRawChangeMs = nowMs;

  s_bootWindowEndMs = nowMs + BOOT_WINDOW_MS;
  s_pressCount = 0;
  s_debugEnabled = false;
  s_debugEventPending = false;
  s_shortPressPending = false;
  s_longPressPending = false;
  s_pressStartedMs = nowMs;
  s_longPressFired = false;
}

void buttonTick(uint32_t nowMs)
{
  const bool rawPressed = readPressedRaw();
  if (rawPressed != s_lastRawPressed) {
    s_lastRawPressed = rawPressed;
    s_lastRawChangeMs = nowMs;
  }

  if ((nowMs - s_lastRawChangeMs) >= DEBOUNCE_MS && s_stablePressed != s_lastRawPressed) {
    s_stablePressed = s_lastRawPressed;

    if (s_stablePressed) {
      s_pressStartedMs = nowMs;
      s_longPressFired = false;

      if ((int32_t)(s_bootWindowEndMs - nowMs) >= 0) {
        if (s_pressCount < 255) {
          s_pressCount++;
        }

        if (!s_debugEnabled && s_pressCount >= REQUIRED_PRESSES) {
          s_debugEnabled = true;
          s_debugEventPending = true;
        }
      }
    } else if (!s_longPressFired) {
      s_shortPressPending = true;
    }
  }

  if (s_stablePressed && !s_longPressFired && (nowMs - s_pressStartedMs >= LONG_PRESS_MS)) {
    s_longPressFired = true;
    s_longPressPending = true;
  }
}

bool buttonConsumeLongPress()
{
  const bool pending = s_longPressPending;
  s_longPressPending = false;
  return pending;
}

bool buttonConsumeShortPress()
{
  const bool pending = s_shortPressPending;
  s_shortPressPending = false;
  return pending;
}

bool buttonIsDebugEnabled()
{
  return s_debugEnabled;
}

void buttonEnableDebug()
{
  if (!s_debugEnabled) {
    s_debugEnabled = true;
    s_debugEventPending = true;
  }
}

bool buttonConsumeDebugEnabledEvent()
{
  const bool pending = s_debugEventPending;
  s_debugEventPending = false;
  return pending;
}
