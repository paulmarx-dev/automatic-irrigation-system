#include "leds.h"

#include <Arduino.h>

static uint8_t s_ledPin = 255;
static bool s_activeHigh = true;
static LedMode s_mode = LED_MODE_OFF;
static LedMode s_onceRestoreMode = LED_MODE_OFF;
static uint8_t s_patternStep = 0;
static uint32_t s_patternStepStartedMs = 0;

static uint32_t s_pulseUntilMs = 0;

struct LedPatternStep {
  bool on;
  uint16_t durationMs;
};

struct LedPattern {
  const LedPatternStep* steps;
  uint8_t count;
  bool repeat;
};

static const LedPatternStep PATTERN_BOOT[] = {
  {true, 500}, {false, 150}, {true, 500}, {false, 150}
};

static const LedPatternStep PATTERN_PAIRING_OPEN[] = {
  {true, 100}, {false, 100}, {true, 100}, {false, 700}
};

static const LedPatternStep PATTERN_JOINING[] = {
  {true, 300}, {false, 700}
};

static const LedPatternStep PATTERN_SUCCESS[] = {
  {true, 3000}, {false, 600}
};

static const LedPatternStep PATTERN_SUCCESS_DOUBLE[] = {
  {true, 1500}, {false, 300}, {true, 1500}, {false, 300}
};

static const LedPatternStep PATTERN_ERROR[] = {
  {true, 100}, {false, 100}, {true, 100}, {false, 100}, {true, 100}, {false, 100}
};

static const LedPatternStep PATTERN_FACTORY_RESET[] = {
  {true, 80}, {false, 80}, {true, 80}, {false, 80}, {true, 80}, {false, 80},
  {true, 80}, {false, 80}, {true, 80}, {false, 80}, {true, 80}, {false, 80}
};

static const LedPatternStep PATTERN_CAL_ENTER[] = {
  {true, 80}, {false, 80}, {true, 80}, {false, 80}, {true, 80},
  {false, 80}, {true, 80}, {false, 80}, {true, 80}, {false, 80}
};

static const LedPatternStep PATTERN_CAL_MEASURE[] = {
  {true, 500}, {false, 500}
};

static const LedPatternStep PATTERN_CAL_PROMPT_WET[] = {
  {true, 400}, {false, 400}, {true, 400}, {false, 800}
};

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

static bool modeIsOneShot(LedMode mode)
{
  return mode == LED_MODE_BOOT ||
         mode == LED_MODE_DEBUG_CONFIRM ||
         mode == LED_MODE_SUCCESS_ONCE ||
        mode == LED_MODE_SUCCESS_DOUBLE ||
      mode == LED_MODE_ERROR_ONCE ||
         mode == LED_MODE_FACTORY_RESET_ONCE ||
         mode == LED_MODE_CAL_ENTER_ONCE ||
         mode == LED_MODE_CAL_DONE_ONCE;
}

static LedPattern getPatternForMode(LedMode mode)
{
  switch (mode) {
    case LED_MODE_BOOT:
      return {PATTERN_BOOT, static_cast<uint8_t>(sizeof(PATTERN_BOOT) / sizeof(PATTERN_BOOT[0])), false};
    case LED_MODE_PAIRING_OPEN:
      return {PATTERN_PAIRING_OPEN, static_cast<uint8_t>(sizeof(PATTERN_PAIRING_OPEN) / sizeof(PATTERN_PAIRING_OPEN[0])), true};
    case LED_MODE_JOINING:
      return {PATTERN_JOINING, static_cast<uint8_t>(sizeof(PATTERN_JOINING) / sizeof(PATTERN_JOINING[0])), true};
    case LED_MODE_DEBUG_CONFIRM:
    case LED_MODE_SUCCESS_ONCE:
    case LED_MODE_CAL_DONE_ONCE:
      return {PATTERN_SUCCESS, static_cast<uint8_t>(sizeof(PATTERN_SUCCESS) / sizeof(PATTERN_SUCCESS[0])), false};
    case LED_MODE_SUCCESS_DOUBLE:
      return {PATTERN_SUCCESS_DOUBLE, static_cast<uint8_t>(sizeof(PATTERN_SUCCESS_DOUBLE) / sizeof(PATTERN_SUCCESS_DOUBLE[0])), false};
    case LED_MODE_ERROR_REPEAT:
    case LED_MODE_CAL_ERROR_REPEAT:
      return {PATTERN_ERROR, static_cast<uint8_t>(sizeof(PATTERN_ERROR) / sizeof(PATTERN_ERROR[0])), true};
    case LED_MODE_ERROR_ONCE:
      return {PATTERN_ERROR, static_cast<uint8_t>(sizeof(PATTERN_ERROR) / sizeof(PATTERN_ERROR[0])), false};
    case LED_MODE_FACTORY_RESET_ONCE:
      return {PATTERN_FACTORY_RESET, static_cast<uint8_t>(sizeof(PATTERN_FACTORY_RESET) / sizeof(PATTERN_FACTORY_RESET[0])), false};
    case LED_MODE_CAL_ENTER_ONCE:
      return {PATTERN_CAL_ENTER, static_cast<uint8_t>(sizeof(PATTERN_CAL_ENTER) / sizeof(PATTERN_CAL_ENTER[0])), false};
    case LED_MODE_CAL_MEASURE_DRY:
    case LED_MODE_CAL_MEASURE_WET:
      return {PATTERN_CAL_MEASURE, static_cast<uint8_t>(sizeof(PATTERN_CAL_MEASURE) / sizeof(PATTERN_CAL_MEASURE[0])), true};
    case LED_MODE_CAL_PROMPT_WET:
      return {PATTERN_CAL_PROMPT_WET, static_cast<uint8_t>(sizeof(PATTERN_CAL_PROMPT_WET) / sizeof(PATTERN_CAL_PROMPT_WET[0])), true};
    case LED_MODE_IDLE:
    case LED_MODE_OFF:
    default:
      return {nullptr, 0, false};
  }
}

static void startMode(LedMode mode, uint32_t nowMs)
{
  s_mode = mode;
  s_patternStep = 0;
  s_patternStepStartedMs = nowMs;
}

void ledsInit(uint8_t pin, bool activeHigh)
{
  s_ledPin = pin;
  s_activeHigh = activeHigh;

  pinMode(s_ledPin, OUTPUT);
  writeLed(false);

  const uint32_t nowMs = millis();
  s_pulseUntilMs = 0;
  s_onceRestoreMode = LED_MODE_OFF;

  startMode(LED_MODE_BOOT, nowMs);
}

void ledsSetMode(LedMode mode)
{
  const uint32_t nowMs = millis();

  if (modeIsOneShot(mode)) {
    if (!modeIsOneShot(s_mode)) {
      s_onceRestoreMode = s_mode;
    }
    startMode(mode, nowMs);
    return;
  }

  if (modeIsOneShot(s_mode)) {
    s_onceRestoreMode = mode;
    return;
  }

  startMode(mode, nowMs);
}

void ledsSetBaseMode(LedMode mode)
{
  ledsSetMode(mode);
}

void ledsTriggerOnce(LedMode mode)
{
  ledsSetMode(mode);
}

void ledsPulseOnce(uint16_t onMs)
{
  const uint32_t nowMs = millis();
  const uint32_t untilMs = nowMs + (uint32_t)onMs;
  if ((int32_t)(untilMs - s_pulseUntilMs) > 0) {
    s_pulseUntilMs = untilMs;
  }
}

static bool tickPattern(uint32_t nowMs, bool* patternOn)
{
  const LedPattern pattern = getPatternForMode(s_mode);
  if (!pattern.steps || pattern.count == 0) {
    *patternOn = false;
    return false;
  }

  if (s_patternStep >= pattern.count) {
    s_patternStep = 0;
    s_patternStepStartedMs = nowMs;
  }

  const LedPatternStep step = pattern.steps[s_patternStep];
  *patternOn = step.on;

  if ((nowMs - s_patternStepStartedMs) < step.durationMs) {
    return true;
  }

  s_patternStep++;
  s_patternStepStartedMs = nowMs;

  if (s_patternStep < pattern.count) {
    return true;
  }

  if (pattern.repeat) {
    s_patternStep = 0;
    return true;
  }

  if (s_mode == LED_MODE_FACTORY_RESET_ONCE) {
    startMode(LED_MODE_SUCCESS_ONCE, nowMs);
    return true;
  }

  if (modeIsOneShot(s_mode)) {
    startMode(s_onceRestoreMode, nowMs);
    return true;
  }

  startMode(LED_MODE_OFF, nowMs);
  return true;
}

void ledsTick(uint32_t nowMs)
{
  bool baseOn = false;
  (void)tickPattern(nowMs, &baseOn);

  const bool pulseOn = (int32_t)(s_pulseUntilMs - nowMs) > 0;
  writeLed(baseOn || pulseOn);
}
