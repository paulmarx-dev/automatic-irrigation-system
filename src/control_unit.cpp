#include <Arduino.h>
#include "leds.h"
#include "button.h"

#if defined(DEVICE_ROLE_CONTROL)

static const char *DEVICE_ROLE = "CONTROL";
static const char *DEVICE_ID = "2";

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("Automatic Irrigation Control boot");
  Serial.print("Role: ");
  Serial.print(DEVICE_ROLE);
  Serial.print(", Device ID: ");
  Serial.println(DEVICE_ID);

  ledsInit(LED_CONTROL_CONFIG.pin, LED_CONTROL_CONFIG.activeHigh);
  buttonInit(BUTTON_CONTROL_CONFIG.pin, BUTTON_CONTROL_CONFIG.activeLow, BUTTON_CONTROL_CONFIG.usePullup);
  ledsSetMode(LED_MODE_IDLE);
}

void loop() {
  const uint32_t now = millis();
  buttonTick(now);
  if (buttonConsumeDebugEnabledEvent()) {
    Serial.println("DEBUG gate: enabled for this boot");
    ledsSetMode(LED_MODE_DEBUG_CONFIRM);
  }
  ledsTick(now);
  delay(10);
}

#endif
