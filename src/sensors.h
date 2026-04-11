#pragma once

#include <Arduino.h>

static constexpr int32_t MOISTURE_DRY_RAW_MV_DEFAULT = 2770;
static constexpr int32_t MOISTURE_WET_RAW_MV_DEFAULT = 1120;
static constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
static constexpr float VOLTAGE_CORRECTION_FACTOR = 1.033f;

struct SensorMeasurement {
	uint16_t moistureRawMv;
	uint16_t moisturePermille;
	float moisturePercentage;
	uint16_t batteryRawMv;
	uint16_t batteryEstMv;
	float batteryPinVoltage;
	float batteryEstimatedVoltage;
};

void setupSensors();
uint16_t readMoistureMilliVolts();
void sensorsResetMoistureCalibrationToDefault();
void sensorsSetMoistureCalibration(int32_t dryMv, int32_t wetMv);
void sensorsGetMoistureCalibration(int32_t* outDryMv, int32_t* outWetMv);
SensorMeasurement measureSensors();
SensorMeasurement measureBatteryOnly();
