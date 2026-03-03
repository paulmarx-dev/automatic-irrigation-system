#include <Arduino.h>

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
}

void loop() {
  delay(1000);
}

#endif
