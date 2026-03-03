#include "sensors.h"

// Kalibrierwerte jetzt in mV (weil moistureRaw jetzt mV ist!)
static int DRY_VALUE = 2770; // mV
static int WET_VALUE = 1120; // mV

static const uint8_t VOLTAGE_SENSOR_PIN = 3;
static const uint8_t MOISTURE_SENSOR_PIN = 4;

static const uint8_t ADC_SAMPLES = 32;
static const uint8_t MOISTURE_DUMMY_SAMPLES = 4;

static const float BATTERY_DIVIDER_RATIO = 2.0f;      // z.B. 470k/470k
static const float VOLTAGE_CORRECTION_FACTOR = 1.033f; // optional: Feinkalibrierung


void setupSensors() {
    pinMode(VOLTAGE_SENSOR_PIN, INPUT);
    pinMode(MOISTURE_SENSOR_PIN, INPUT);

    analogReadResolution(12);
    analogSetPinAttenuation(VOLTAGE_SENSOR_PIN, ADC_11db);
    analogSetPinAttenuation(MOISTURE_SENSOR_PIN, ADC_11db);
}

static uint16_t readAveragedMilliVolts(uint8_t pin, uint8_t sampleCount) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < sampleCount; ++i) {
        sum += analogReadMilliVolts(pin);
        delay(2);
    }
    return static_cast<uint16_t>(sum / sampleCount); // mV
}

static uint16_t readBatteryMilliVoltsAtAdcPin() {
    // Dummy reads (auch in mV, konsistent)
    (void)analogReadMilliVolts(VOLTAGE_SENSOR_PIN);
    delay(2);
    (void)analogReadMilliVolts(VOLTAGE_SENSOR_PIN);
    delay(2);

    return readAveragedMilliVolts(VOLTAGE_SENSOR_PIN, ADC_SAMPLES); // mV am ADC-Pin
}

static uint16_t readMoistureMilliVolts() {
    for (uint8_t i = 0; i < MOISTURE_DUMMY_SAMPLES; ++i) {
        (void)analogReadMilliVolts(MOISTURE_SENSOR_PIN);
        delay(2);
    }
    return readAveragedMilliVolts(MOISTURE_SENSOR_PIN, ADC_SAMPLES); // mV
}

SensorMeasurement measureSensors() {
    SensorMeasurement measurement{};

    // Battery
    measurement.batteryRaw = readBatteryMilliVoltsAtAdcPin(); // jetzt: mV (am ADC-Pin)
    measurement.batteryPinVoltage = measurement.batteryRaw / 1000.0f; // Volt am ADC-Pin

    // Auf Batteriespannung hochrechnen (Teiler)
    measurement.batteryEstimatedVoltage =
        (measurement.batteryPinVoltage * BATTERY_DIVIDER_RATIO) * VOLTAGE_CORRECTION_FACTOR;

    // Moisture
    measurement.moistureRaw = readMoistureMilliVolts(); // jetzt: mV

    // Prozent: bei dir offenbar "nass => kleiner mV", "trocken => größer mV"
    float pct = (static_cast<float>(DRY_VALUE) - static_cast<float>(measurement.moistureRaw)) *
                100.0f /
                (static_cast<float>(DRY_VALUE) - static_cast<float>(WET_VALUE));

    pct = constrain(pct, 0.0f, 100.0f);
    measurement.moisturePercentage = pct;

    return measurement;
}
