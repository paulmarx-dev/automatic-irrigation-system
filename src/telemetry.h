#pragma once

#include <stdint.h>

struct SensorMeasurement;

enum TelemetryHeadNodeState : uint8_t {
  TELEMETRY_HEAD_NODE_ONLINE = 0,
  TELEMETRY_HEAD_NODE_SUSPECT = 1,
  TELEMETRY_HEAD_NODE_OFFLINE = 2,
};

enum TelemetryHeadBatteryState : uint8_t {
	TELEMETRY_HEAD_BATTERY_OK = 0,
	TELEMETRY_HEAD_BATTERY_CRITICAL = 1,
	TELEMETRY_HEAD_BATTERY_NEEDS_REPLACEMENT = 2,
};

struct TelemetryHeadNodePresence {
	bool used;
	TelemetryHeadNodeState state;
	TelemetryHeadBatteryState batteryState;
	uint16_t nodeId;
	uint16_t moisturePermille;
	uint16_t batteryEstMv;
	uint8_t mac[6];
	uint32_t lastSeenMs;
	uint32_t rxPackets;
	uint32_t rxDuplicates;
	uint32_t rxInvalid;
	uint32_t ackOkSent;
	uint32_t ackNotPairedSent;
};

void telemetryInit();
void telemetryOnRecv(const uint8_t* src_mac, const uint8_t* data, int len);
void telemetryTickSensor(const SensorMeasurement* measurement, bool hasMeasurement, uint32_t nowMs);
void telemetryTickHead(uint32_t nowMs);
uint8_t telemetryHeadGetPresence(TelemetryHeadNodePresence* outNodes, uint8_t maxNodes);
void telemetryHeadClearPresence();
bool telemetryHeadRemovePresenceByNodeId(uint16_t nodeId);
bool telemetryHeadSendRemoteButtonAction(uint16_t nodeId, uint8_t action);
bool telemetryHeadSendIrrigationState(uint8_t desiredState, uint32_t leaseId, uint32_t remainingLeaseMs);
