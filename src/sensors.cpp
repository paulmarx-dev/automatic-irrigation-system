#include "sensors.h"

static const uint8_t VOLTAGE_SENSOR_PIN = 3;
static const uint8_t MOISTURE_SENSOR_PIN = 4;

static const uint8_t ADC_SAMPLES = 32;
static const uint8_t MOISTURE_DUMMY_SAMPLES = 4;

static int32_t s_moistureDryRawMv = MOISTURE_DRY_RAW_MV_DEFAULT;
static int32_t s_moistureWetRawMv = MOISTURE_WET_RAW_MV_DEFAULT;

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
    // Dummy reads in mV for ADC stabilization
    (void)analogReadMilliVolts(VOLTAGE_SENSOR_PIN);
    delay(2);
    (void)analogReadMilliVolts(VOLTAGE_SENSOR_PIN);
    delay(2);

    return readAveragedMilliVolts(VOLTAGE_SENSOR_PIN, ADC_SAMPLES); // mV at ADC pin
}

uint16_t readMoistureMilliVolts() {
    for (uint8_t i = 0; i < MOISTURE_DUMMY_SAMPLES; ++i) {
        (void)analogReadMilliVolts(MOISTURE_SENSOR_PIN);
        delay(2);
    }
    return readAveragedMilliVolts(MOISTURE_SENSOR_PIN, ADC_SAMPLES); // mV
}

static uint16_t computeMoisturePermilleFromRaw(uint16_t moistureRawMv) {
    int32_t dryMv = s_moistureDryRawMv;
    int32_t wetMv = s_moistureWetRawMv;
    if (dryMv < wetMv) {
        const int32_t tmp = dryMv;
        dryMv = wetMv;
        wetMv = tmp;
    }

    const int32_t denominator = dryMv - wetMv;
    if (denominator == 0) {
        return 0;
    }

    const int32_t numerator =
        (dryMv - (int32_t)moistureRawMv) * 1000;
    int32_t permille = numerator / denominator;
    permille = constrain(permille, 0, 1000);
    return (uint16_t)permille;
}

void sensorsResetMoistureCalibrationToDefault() {
    s_moistureDryRawMv = MOISTURE_DRY_RAW_MV_DEFAULT;
    s_moistureWetRawMv = MOISTURE_WET_RAW_MV_DEFAULT;
}

void sensorsSetMoistureCalibration(int32_t dryMv, int32_t wetMv) {
    if (dryMv == wetMv) {
        sensorsResetMoistureCalibrationToDefault();
        return;
    }
    s_moistureDryRawMv = dryMv;
    s_moistureWetRawMv = wetMv;
}

void sensorsGetMoistureCalibration(int32_t* outDryMv, int32_t* outWetMv) {
    if (outDryMv) {
        *outDryMv = s_moistureDryRawMv;
    }
    if (outWetMv) {
        *outWetMv = s_moistureWetRawMv;
    }
}

static uint16_t computeBatteryEstimatedMvFromRaw(uint16_t batteryRawMv) {
    const float estimatedMv =
        batteryRawMv * BATTERY_DIVIDER_RATIO * VOLTAGE_CORRECTION_FACTOR;
    if (estimatedMv <= 0.0f) {
        return 0;
    }
    return static_cast<uint16_t>(estimatedMv + 0.5f);
}

SensorMeasurement measureSensors() {
    SensorMeasurement measurement{};

    // Battery
    measurement.batteryRawMv = readBatteryMilliVoltsAtAdcPin();
    measurement.batteryEstMv = computeBatteryEstimatedMvFromRaw(measurement.batteryRawMv);
    measurement.batteryPinVoltage = measurement.batteryRawMv / 1000.0f; // debug only
    measurement.batteryEstimatedVoltage = measurement.batteryEstMv / 1000.0f; // debug only

    // Moisture
    measurement.moistureRawMv = readMoistureMilliVolts();
    measurement.moisturePermille = computeMoisturePermilleFromRaw(measurement.moistureRawMv);
    measurement.moisturePercentage = measurement.moisturePermille / 10.0f; // debug only

    return measurement;
}

SensorMeasurement measureBatteryOnly() {
    SensorMeasurement measurement{};

    measurement.batteryRawMv = readBatteryMilliVoltsAtAdcPin();
    measurement.batteryEstMv = computeBatteryEstimatedMvFromRaw(measurement.batteryRawMv);
    measurement.batteryPinVoltage = measurement.batteryRawMv / 1000.0f;
    measurement.batteryEstimatedVoltage = measurement.batteryEstMv / 1000.0f;

    measurement.moistureRawMv = 0;
    measurement.moisturePermille = 0;
    measurement.moisturePercentage = 0.0f;

    return measurement;
}
