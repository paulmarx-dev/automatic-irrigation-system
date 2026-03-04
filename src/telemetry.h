#pragma once

#include <stdint.h>

struct SensorMeasurement;

struct TelemetryHeadNodePresence {
	bool used;
	bool online;
	uint16_t nodeId;
	uint8_t mac[6];
	uint32_t lastSeenMs;
};

void telemetryInit();
void telemetryOnRecv(const uint8_t* src_mac, const uint8_t* data, int len);
void telemetryTickSensor(const SensorMeasurement* measurement, bool hasMeasurement, uint32_t nowMs);
void telemetryTickHead(uint32_t nowMs);
uint8_t telemetryHeadGetPresence(TelemetryHeadNodePresence* outNodes, uint8_t maxNodes);
