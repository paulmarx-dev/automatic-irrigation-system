#include <Arduino.h>
#include "sensors.h"
#include "common_config.h"
#include "esp_now_helpers.h"
#include <WiFi.h>
#include "pairing.h"
#include "pairing_nvs.h"
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
static bool s_autoJoinTriggered = false;

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
	uint8_t restoredHeadMac[6] = {0};
	if (pairingNvsLoadNode(restoredHeadMac)) {
		pairingNodeRestorePairedHead(restoredHeadMac);
		(void)espnowEnsurePeer(restoredHeadMac, ESPNOW_CHANNEL, false);
		s_autoJoinTriggered = true;
		Serial.println("PAIRING(NODE): restored paired head from NVS");
	}
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
	pairingNodeTick(now);

	if (!s_autoJoinTriggered && !pairingNodeIsPaired()) {
		pairingNodeEnterJoinMode(now);
		Serial.println("PAIRING(NODE): auto-join on boot");
		s_autoJoinTriggered = true;
	}

	static bool wasPaired = false;
	static bool pairStateInitialized = false;
	static bool lastJoinModeActive = false;
	const bool isPaired = pairingNodeIsPaired();
	bool joinModeActive = pairingNodeIsInJoinMode();

	if (!pairStateInitialized) {
		wasPaired = isPaired;
		pairStateInitialized = true;
	}

	if (buttonConsumeLongPress()) {
		Serial.println("PAIRING(NODE): factory reset requested");
		pairingNodeFactoryReset();
		if (!pairingNvsClearNode()) {
			Serial.println("PAIRING(NODE): NVS clear failed");
		}
		pairingNodeEnterJoinMode(now);
		s_autoJoinTriggered = true;
		Serial.println("PAIRING(NODE): join window opened after factory reset");
		joinModeActive = true;
		ledsTriggerOnce(LED_MODE_FACTORY_RESET_ONCE);
	}

	if (!wasPaired && isPaired) {
		uint8_t headMac[6] = {0};
		if (pairingNodeHeadMac(headMac)) {
			if (!pairingNvsSaveNode(headMac)) {
				Serial.println("PAIRING(NODE): NVS save failed");
			}
		}
		Serial.println("PAIRING(NODE): join success");
		ledsTriggerOnce(LED_MODE_SUCCESS_ONCE);
	}
	wasPaired = isPaired;

	if (!isPaired && joinModeActive && pairingNodeJoinExpired(now)) {
		pairingNodeExitJoinMode();
		Serial.println("PAIRING(NODE): join window expired");
		ledsTriggerOnce(LED_MODE_ERROR_ONCE);
		joinModeActive = false;
	}

	if (buttonConsumeShortPress()) {
		if (isPaired) {
			Serial.println("PAIRING(NODE): short press ignored (already paired)");
		} else if (pairingNodeIsInJoinMode()) {
			pairingNodeExitJoinMode();
			Serial.println("PAIRING(NODE): join window canceled by user");
			ledsTriggerOnce(LED_MODE_ERROR_ONCE);
			joinModeActive = false;
		} else {
			pairingNodeEnterJoinMode(now);
			Serial.println("PAIRING(NODE): join window opened");
			ledsSetBaseMode(LED_MODE_JOINING);
			joinModeActive = true;
		}
	}

	if (buttonConsumeDebugEnabledEvent()) {
		Serial.println("DEBUG gate: enabled for this boot");
		ledsTriggerOnce(LED_MODE_DEBUG_CONFIRM);
	}

	const bool joinModeNow = pairingNodeIsInJoinMode();
	if (joinModeNow && !lastJoinModeActive) {
		ledsSetBaseMode(LED_MODE_JOINING);
	} else if (!joinModeNow && lastJoinModeActive) {
		ledsSetBaseMode(LED_MODE_OFF);
	}
	lastJoinModeActive = joinModeNow;

	ledsTick(now);

	if (now - lastMeasurement >= MEASUREMENT_INTERVAL_MS) {
		lastMeasurement = now;
		latestMeasurement = measureSensors();
		haveMeasurement = true;
	}
	telemetryTickSensor(haveMeasurement ? &latestMeasurement : nullptr, haveMeasurement, now);
}

#endif


