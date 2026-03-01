#include <Arduino.h>

#if defined(DEVICE_ROLE_SENSOR)
static const char *DEVICE_ROLE = "SENSOR";
static const uint8_t SENSOR_LED_PIN = 8;
static const bool SENSOR_LED_ACTIVE_LOW = true;
static const unsigned long BLINK_INTERVAL_MS = 500;
#elif defined(DEVICE_ROLE_HEAD)
static const char *DEVICE_ROLE = "HEAD";
static const uint8_t HEAD_RGB_PIN = 8;
static const uint8_t HEAD_RGB_BRIGHTNESS = 32;
static const unsigned long BLINK_INTERVAL_MS = 500;
static const unsigned long HEAD_BLINK_PHASE_MS = 3000;
static const unsigned long HEAD_COLOR_PHASE_MS = 10000;
static const unsigned long HEAD_COLOR_STEP_MS = 200;
#else
static const char *DEVICE_ROLE = "UNDEFINED";
#endif

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("Automatic Irrigation System booting...");
  Serial.print("Role: ");
  Serial.println(DEVICE_ROLE);

#if defined(DEVICE_ROLE_SENSOR)
  pinMode(SENSOR_LED_PIN, OUTPUT);
  digitalWrite(SENSOR_LED_PIN, SENSOR_LED_ACTIVE_LOW ? HIGH : LOW);
  Serial.print("Blink test pin: ");
  Serial.println(SENSOR_LED_PIN);
  Serial.println("Blink polarity: active-low");
#elif defined(DEVICE_ROLE_HEAD)
  Serial.print("RGB blink pin: ");
  Serial.println(HEAD_RGB_PIN);
  Serial.println("RGB LED test: neopixelWrite blue blink");
  rgbLedWrite(HEAD_RGB_PIN, 0, 0, 0);
#endif
}

void loop() {
#if defined(DEVICE_ROLE_SENSOR)
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  const unsigned long now = millis();

  if (now - lastBlink >= BLINK_INTERVAL_MS) {
    lastBlink = now;
    ledState = !ledState;
    const uint8_t ledOnLevel = SENSOR_LED_ACTIVE_LOW ? LOW : HIGH;
    const uint8_t ledOffLevel = SENSOR_LED_ACTIVE_LOW ? HIGH : LOW;
    digitalWrite(SENSOR_LED_PIN, ledState ? ledOnLevel : ledOffLevel);
  }
#elif defined(DEVICE_ROLE_HEAD)
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  static int lastColorIndex = -1;
  const unsigned long now = millis();
  const unsigned long cycleDuration = HEAD_BLINK_PHASE_MS + HEAD_COLOR_PHASE_MS;
  const unsigned long cycleElapsed = now % cycleDuration;

  if (cycleElapsed < HEAD_BLINK_PHASE_MS) {
    if (now - lastBlink >= BLINK_INTERVAL_MS) {
      lastBlink = now;
      ledState = !ledState;
      if (ledState) {
        rgbLedWrite(HEAD_RGB_PIN, 0, 0, HEAD_RGB_BRIGHTNESS);
      } else {
        rgbLedWrite(HEAD_RGB_PIN, 0, 0, 0);
      }
    }
    lastColorIndex = -1;
  } else {
    const unsigned long colorElapsed = cycleElapsed - HEAD_BLINK_PHASE_MS;
    const int colorIndex = (colorElapsed / HEAD_COLOR_STEP_MS) % 6;

    if (colorIndex != lastColorIndex) {
      lastColorIndex = colorIndex;
      switch (colorIndex) {
        case 0:
          rgbLedWrite(HEAD_RGB_PIN, HEAD_RGB_BRIGHTNESS, 0, 0);
          break;
        case 1:
          rgbLedWrite(HEAD_RGB_PIN, 0, HEAD_RGB_BRIGHTNESS, 0);
          break;
        case 2:
          rgbLedWrite(HEAD_RGB_PIN, 0, 0, HEAD_RGB_BRIGHTNESS);
          break;
        case 3:
          rgbLedWrite(HEAD_RGB_PIN, HEAD_RGB_BRIGHTNESS, HEAD_RGB_BRIGHTNESS, 0);
          break;
        case 4:
          rgbLedWrite(HEAD_RGB_PIN, 0, HEAD_RGB_BRIGHTNESS, HEAD_RGB_BRIGHTNESS);
          break;
        default:
          rgbLedWrite(HEAD_RGB_PIN, HEAD_RGB_BRIGHTNESS, 0, HEAD_RGB_BRIGHTNESS);
          break;
      }
    }
  }
#else
  delay(1000);
#endif
}