#include <Arduino.h>
#include "sensors.h"
#include "common_config.h"
#include "esp_now_helpers.h"
#include <WiFi.h>
#include "pairing.h"
#include "protocol.h"

#if defined(DEVICE_ROLE_SENSOR)

// *************************************************************************************
// Sensor configuration and measurement implementation
// *************************************************************************************

static const char *DEVICE_ROLE = "SENSOR";
static const char *DEVICE_ID = "3";

static const unsigned long MEASUREMENT_INTERVAL_MS = 1000;
static const unsigned long TELEMETRY_INTERVAL_MS = 5000;

static const uint8_t SENSOR_LED_PIN = 8;
static const bool SENSOR_LED_ACTIVE_LOW = true;
static uint16_t s_telemetrySeq = 0;
static void onRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
	(void)pairingOnRecv(src_mac, data, len);
}

static void onSend(const uint8_t* dst_mac, bool success)
{
    (void)dst_mac;
    (void)success;
}

static void printMac(const char* label, const uint8_t mac[6])
{
    char buf[18] = {0};
    macToString(mac, buf, sizeof(buf));
    Serial.print(label);
    Serial.println(buf);
}




void setup() {
	Serial.begin(115200);
	delay(1500);
	Serial.println("BOOT");

  	setupSensors();

	Serial.println();
	Serial.println("Automatic Irrigation Sensor boot");
	Serial.print("Role: ");
	Serial.print(DEVICE_ROLE);
    Serial.print(", Device ID: "); 
    Serial.println(DEVICE_ID);

	//*******************************************************************************
	// ESP-NOW initialization and pairing
	//*******************************************************************************
	//delay(200);

    Serial.println();
    Serial.println("SENSOR: Pairing 2.0 always-open");

    printMac("SENSOR custom MAC: ", MAC_SENSOR1);

    if (!espnowInit(ESPNOW_CHANNEL, MAC_SENSOR1, onRecv, onSend)) {
        Serial.println("espnowInit() failed");
        while (true) { delay(1000); }
    }

	pairingInitNode(ROLE_SENSOR);


    Serial.println("ESP-NOW ready.");
	Serial.println("USED MAC: " + WiFi.macAddress());

}



void loop() {
    pairingTick();

	static unsigned long lastMeasurement = 0;
	static unsigned long lastTelemetry = 0;
	const unsigned long now = millis();

	if (now - lastMeasurement >= MEASUREMENT_INTERVAL_MS) {
		lastMeasurement = now;

		const SensorMeasurement measurement = measureSensors();

		if (!pairingNodeIsPaired()) {
			return;
		}

		if (now - lastTelemetry < TELEMETRY_INTERVAL_MS) {
			return;
		}
		lastTelemetry = now;

		uint8_t headMac[6] = {0};
		if (!pairingNodeHeadMac(headMac)) {
			return;
		}

		MsgTelemetry telemetry{};
		telemetry.hdr.ver = PROTO_VER;
		telemetry.hdr.type = MSG_TELEMETRY;
		telemetry.hdr.seq = ++s_telemetrySeq;
		telemetry.hdr.nodeId = pairingNodeId();

		int32_t moisturePermille = (int32_t)(measurement.moisturePercentage * 10.0f + 0.5f);
		moisturePermille = constrain(moisturePermille, 0, 1000);

		telemetry.moisturePermille = (uint16_t)moisturePermille;
		telemetry.moistureRawMv = measurement.moistureRaw;
		telemetry.batteryRawMv = measurement.batteryRaw;
		telemetry.batteryEstMv = (uint16_t)(measurement.batteryEstimatedVoltage * 1000.0f);
		telemetry.flags = 0;
		telemetry.reserved = 0;

		(void)espnowEnsurePeer(headMac, ESPNOW_CHANNEL, false);
		const bool sent = espnowSend(headMac, reinterpret_cast<const uint8_t*>(&telemetry), sizeof(telemetry));

		Serial.print("TELEMETRY sent=");
		Serial.print(sent ? 1 : 0);
		Serial.print(" seq=");
		Serial.print((unsigned long)telemetry.hdr.seq);
		Serial.print(" nodeId=");
		Serial.print((unsigned long)telemetry.hdr.nodeId);
		Serial.print(" moisturePermille=");
		Serial.print((unsigned long)telemetry.moisturePermille);
		Serial.print(" moistureRawMv=");
		Serial.print((unsigned long)telemetry.moistureRawMv);
		Serial.print(" batteryRawMv=");
		Serial.print((unsigned long)telemetry.batteryRawMv);
		Serial.print(" batteryEstMv=");
		Serial.print((unsigned long)telemetry.batteryEstMv);
		Serial.print(" flags=");
		Serial.println((unsigned long)telemetry.flags);
  }
}

#endif


