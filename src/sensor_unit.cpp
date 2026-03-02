#include <Arduino.h>
#include "sensors.h"
#include "common_config.h"
#include "esp_now_helpers.h"
#include <WiFi.h>
#include "pairing.h"

#if defined(DEVICE_ROLE_SENSOR)

// *************************************************************************************
// Sensor configuration and measurement implementation
// *************************************************************************************

static const char *DEVICE_ROLE = "SENSOR";
static const char *DEVICE_ID = "3";

static const unsigned long MEASUREMENT_INTERVAL_MS = 1000;

static const uint8_t SENSOR_LED_PIN = 8;
static const bool SENSOR_LED_ACTIVE_LOW = true;
static void onRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
    pairingOnRecv(src_mac, data, len);
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
	const unsigned long now = millis();

	if (now - lastMeasurement >= MEASUREMENT_INTERVAL_MS) {
		lastMeasurement = now;

		const SensorMeasurement measurement = measureSensors();

		// commented out to reduce serial output during pairing development, will re-enable later for sensor validation
		// Serial.print("moisture_raw=");
		// Serial.print(measurement.moistureRaw);
		// Serial.print(", moisture_percentage=");
		// Serial.print(measurement.moisturePercentage, 1);
		// Serial.print(", battery_raw=");
		// Serial.print(measurement.batteryRaw);
		// Serial.print(", battery_pin_v=");
		// Serial.print(measurement.batteryPinVoltage, 3);
		// Serial.print(", battery_est_v=");
		// Serial.println(measurement.batteryEstimatedVoltage, 3);
  }
}

#endif


