#pragma once

#include <Arduino.h>

struct SensorMeasurement {
	uint16_t moistureRaw;
	float moisturePercentage;
	uint16_t batteryRaw;
	float batteryPinVoltage;
	float batteryEstimatedVoltage;
};

void setupSensors();
SensorMeasurement measureSensors();
