#include <Arduino.h>
#include "sensors.h"
#include "common_config.h"
#include "esp_now_helpers.h"
#include <WiFi.h>
#include "pairing.h"
#include "telemetry.h"
#include "leds.h"
#include "button.h"

#if defined(DEVICE_ROLE_SENSOR)

// *************************************************************************************
// Sensor configuration and measurement implementation
// *************************************************************************************

static const char *DEVICE_ROLE = "SENSOR";
static const char *DEVICE_ID = "3";

static const unsigned long MEASUREMENT_INTERVAL_MS = 1000;
static SensorMeasurement latestMeasurement{};
static bool haveMeasurement = false;

enum SensorUxState : uint8_t {
	SENSOR_UX_NORMAL = 0,
	SENSOR_UX_CAL_PROMPT_WET = 1,
	SENSOR_UX_CAL_MEASURE_DRY = 2,
	SENSOR_UX_CAL_MEASURE_WET = 3,
};

static SensorUxState s_uxState = SENSOR_UX_NORMAL;

static void onRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
	if (pairingOnRecv(src_mac, data, len)) {
		return;
	}
	telemetryOnRecv(src_mac, data, len);
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
	telemetryInit();
	ledsInit(LED_DEFAULT_CONFIG.pin, LED_DEFAULT_CONFIG.activeHigh);
	buttonInit(BUTTON_SENSOR_CONFIG.pin, BUTTON_SENSOR_CONFIG.activeLow, BUTTON_SENSOR_CONFIG.usePullup);


    Serial.println("ESP-NOW ready.");
	Serial.println("USED MAC: " + WiFi.macAddress());

}



void loop() {
    pairingTick();

	static unsigned long lastMeasurement = 0;
	const unsigned long now = millis();

	buttonTick(now);
	static bool idleModeSet = false;
	if (s_uxState == SENSOR_UX_NORMAL) {
		if (pairingNodeIsPaired()) {
			if (!idleModeSet) {
				ledsSetMode(LED_MODE_IDLE);
				idleModeSet = true;
			}
		} else {
			idleModeSet = false;
		}
	} else {
		idleModeSet = false;
	}

	if (buttonConsumeDebugEnabledEvent()) {
		Serial.println("DEBUG gate: enabled for this boot");
		ledsSetMode(LED_MODE_DEBUG_CONFIRM);

		// Temporary entry hook for calibration scaffold.
		s_uxState = SENSOR_UX_CAL_PROMPT_WET;
		Serial.println("CAL: prompt wet reference (press button to confirm)");
		ledsSetMode(LED_MODE_CAL_PROMPT_WET);
	}

	if (s_uxState == SENSOR_UX_CAL_PROMPT_WET && buttonConsumeShortPress()) {
		Serial.println("CAL: wet reference confirmed");
		ledsSetMode(LED_MODE_SUCCESS_ONCE);
		s_uxState = SENSOR_UX_NORMAL;
		if (pairingNodeIsPaired()) {
			ledsSetMode(LED_MODE_IDLE);
		} else {
			ledsSetMode(LED_MODE_OFF);
		}
	}

	ledsTick(now);

	if (now - lastMeasurement >= MEASUREMENT_INTERVAL_MS) {
		lastMeasurement = now;
		latestMeasurement = measureSensors();
		haveMeasurement = true;
	}
	telemetryTickSensor(haveMeasurement ? &latestMeasurement : nullptr, haveMeasurement, now);
}

#endif


