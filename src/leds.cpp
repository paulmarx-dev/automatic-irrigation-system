#include "leds.h"

#include <Arduino.h>

static uint8_t s_ledPin = 255;
static bool s_activeHigh = true;
static LedMode s_mode = LED_MODE_OFF;
static LedMode s_restoreMode = LED_MODE_OFF;

static uint8_t s_seqStep = 0;
static uint32_t s_seqStepStartedMs = 0;
static bool s_seqRunning = false;

static uint32_t s_idlePulseStartedMs = 0;
static bool s_idlePulseActive = false;
static uint32_t s_lastIdleBeatMs = 0;

static uint32_t s_pulseUntilMs = 0;

static void writeLed(bool on)
{
  if (s_ledPin == 255) {
    return;
  }

  const bool effectiveOn = s_activeHigh ? on : !on;

#if HAS_RGB_LED
  const uint8_t blue = effectiveOn ? static_cast<uint8_t>(RGB_LED_BRIGHTNESS) : 0;
  rgbLedWrite(s_ledPin, 0, 0, blue);
#else
  const uint8_t level = effectiveOn ? HIGH : LOW;
  digitalWrite(s_ledPin, level);
#endif
}

static void startSequence(LedMode mode, LedMode restoreMode, uint32_t nowMs)
{
  s_mode = mode;
  s_restoreMode = restoreMode;
  s_seqStep = 0;
  s_seqStepStartedMs = nowMs;
  s_seqRunning = true;
}

void ledsInit(uint8_t pin, bool activeHigh)
{
  s_ledPin = pin;
  s_activeHigh = activeHigh;

  pinMode(s_ledPin, OUTPUT);
  writeLed(false);

  const uint32_t nowMs = millis();
  s_idlePulseStartedMs = 0;
  s_idlePulseActive = false;
  s_lastIdleBeatMs = nowMs;
  s_pulseUntilMs = 0;

  startSequence(LED_MODE_BOOT, LED_MODE_OFF, nowMs);
}

void ledsSetMode(LedMode mode)
{
  const uint32_t nowMs = millis();

  if (mode == LED_MODE_DEBUG_CONFIRM) {
    startSequence(LED_MODE_DEBUG_CONFIRM, s_mode == LED_MODE_DEBUG_CONFIRM ? LED_MODE_IDLE : s_mode, nowMs);
    return;
  }

  if (s_seqRunning) {
    s_restoreMode = mode;
    return;
  }

  s_mode = mode;
  s_seqRunning = false;
  s_seqStep = 0;
  s_seqStepStartedMs = nowMs;
  if (mode != LED_MODE_IDLE) {
    s_idlePulseActive = false;
  }
}

void ledsPulseOnce(uint16_t onMs)
{
  const uint32_t nowMs = millis();
  const uint32_t untilMs = nowMs + (uint32_t)onMs;
  if ((int32_t)(untilMs - s_pulseUntilMs) > 0) {
    s_pulseUntilMs = untilMs;
  }
}

static bool tickSequence(uint32_t nowMs)
{
  if (!s_seqRunning) {
    return false;
  }

  const uint16_t onMs = 500;
  const uint16_t offMs = 150;

  switch (s_seqStep) {
    case 0:
      writeLed(true);
      if (nowMs - s_seqStepStartedMs >= onMs) {
        s_seqStep = 1;
        s_seqStepStartedMs = nowMs;
      }
      return true;
    case 1:
      writeLed(false);
      if (nowMs - s_seqStepStartedMs >= offMs) {
        s_seqStep = 2;
        s_seqStepStartedMs = nowMs;
      }
      return true;
    case 2:
      writeLed(true);
      if (nowMs - s_seqStepStartedMs >= onMs) {
        s_seqStep = 3;
        s_seqStepStartedMs = nowMs;
      }
      return true;
    case 3:
      writeLed(false);
      if (nowMs - s_seqStepStartedMs >= offMs) {
        s_seqRunning = false;
        s_mode = s_restoreMode;
      }
      return true;
    default:
      s_seqRunning = false;
      return false;
  }
}

void ledsTick(uint32_t nowMs)
{
  if (tickSequence(nowMs)) {
    return;
  }

  bool baseOn = false;

  if (s_mode == LED_MODE_IDLE) {
    // IDLE heartbeat is intentionally disabled for now.
    // Keep this logic for quick restore later:
    // const uint32_t heartbeatPeriodMs = 2000;
    // const uint32_t heartbeatOnMs = 35;
    // if (!s_idlePulseActive && (nowMs - s_lastIdleBeatMs >= heartbeatPeriodMs)) {
    //   s_idlePulseActive = true;
    //   s_idlePulseStartedMs = nowMs;
    //   s_lastIdleBeatMs = nowMs;
    // }
    // if (s_idlePulseActive) {
    //   if (nowMs - s_idlePulseStartedMs < heartbeatOnMs) {
    //     baseOn = true;
    //   } else {
    //     s_idlePulseActive = false;
    //   }
    // }
    baseOn = false;
  }

  const bool pulseOn = (int32_t)(s_pulseUntilMs - nowMs) > 0;
  writeLed(baseOn || pulseOn);
}
