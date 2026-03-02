#pragma once

#include <Arduino.h>

static constexpr int32_t MOISTURE_DRY_RAW_MV_DEFAULT = 2770;
static constexpr int32_t MOISTURE_WET_RAW_MV_DEFAULT = 1120;
static constexpr uint32_t BATTERY_EST_RATIO_PERMILLE_DEFAULT = 2066;

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
SensorMeasurement measureSensors();
