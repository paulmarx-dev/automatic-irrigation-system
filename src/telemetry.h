#pragma once

#include <stdint.h>

struct SensorMeasurement;

void telemetryInit();
void telemetryOnRecv(const uint8_t* src_mac, const uint8_t* data, int len);
void telemetryTickSensor(const SensorMeasurement* measurement, bool hasMeasurement, uint32_t nowMs);
