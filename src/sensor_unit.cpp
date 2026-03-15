#include <Arduino.h>
#include "sensors.h"
#include "common_config.h"
#include "esp_now_helpers.h"
#include <WiFi.h>
#include "pairing.h"
#include "pairing_nvs.h"
#include "cal_nvs.h"
#include "telemetry.h"
#include "protocol.h"
#include "sensor_remote_control.h"
#include "leds.h"
#include "button.h"
#include "app_log.h"
#include "sleep_logic.h"

#if defined(DEVICE_ROLE_SENSOR)

// *************************************************************************************
// Sensor configuration and measurement implementation
// *************************************************************************************

static const char *DEVICE_ROLE = "SENSOR";
static const char *DEVICE_ID = "3";

static const unsigned long MEASUREMENT_INTERVAL_MS = 1000;
static const uint32_t MULTIPRESS_WINDOW_MS = 1400;
static constexpr int32_t CAL_MIN_DELTA_MV = 300;
static constexpr uint32_t CAL_PROMPT_TIMEOUT_MS = 20000;
static constexpr uint32_t CAL_SETTLE_MS = 1000;
static constexpr uint8_t CAL_MEDIAN_SAMPLES = 5;
static constexpr uint32_t CAL_SAMPLE_INTERVAL_MS = 70;

static SensorMeasurement latestMeasurement{};
static bool haveMeasurement = false;
static bool s_autoJoinTriggered = false;

enum CalibrationState : uint8_t {
	CAL_STATE_IDLE = 0,
	CAL_STATE_ENTER,
	CAL_STATE_MEASURE_DRY_WAIT,
	CAL_STATE_PROMPT_WET,
	CAL_STATE_MEASURE_WET_WAIT,
};

struct PressArbEvents {
	bool single;
	bool triple;
	bool debug;
};

static uint8_t s_pressCount = 0;
static uint32_t s_pressWindowDeadlineMs = 0;

static bool s_calibrationActive = false;
static CalibrationState s_calibrationState = CAL_STATE_IDLE;
static uint32_t s_calibrationStateStartedMs = 0;
static bool s_calibrationCollecting = false;
static uint8_t s_calibrationSampleIndex = 0;
static uint32_t s_calibrationNextSampleMs = 0;
static uint16_t s_calibrationSamples[CAL_MEDIAN_SAMPLES] = {0};
static int32_t s_calibrationDryMv = 0;
static int32_t s_calibrationWetMv = 0;
static bool s_remoteCalibrateStartPending = false;
static bool s_remoteMeasureWetPending = false;

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

static void restoreBaseLedFromState()
{
	if (pairingNodeIsInJoinMode()) {
		ledsSetBaseMode(LED_MODE_JOINING);
	} else {
		ledsSetBaseMode(LED_MODE_OFF);
	}
}

static void calibrationExit()
{
	s_calibrationActive = false;
	s_calibrationState = CAL_STATE_IDLE;
	s_calibrationCollecting = false;
	restoreBaseLedFromState();
}

static void calibrationEnter(uint32_t now)
{
	s_calibrationActive = true;
	s_calibrationState = CAL_STATE_ENTER;
	s_calibrationStateStartedMs = now;
	s_calibrationCollecting = false;
	s_calibrationSampleIndex = 0;
	s_calibrationNextSampleMs = 0;
	s_calibrationDryMv = 0;
	s_calibrationWetMv = 0;

	ledsSetBaseMode(LED_MODE_OFF);
	ledsTriggerOnce(LED_MODE_CAL_ENTER_ONCE);
	Serial.println("CAL: enter");

	s_calibrationState = CAL_STATE_MEASURE_DRY_WAIT;
	s_calibrationStateStartedMs = now;
	ledsSetBaseMode(LED_MODE_CAL_MEASURE_DRY);
	Serial.println("CAL: measure dry start");
}

static uint16_t medianOfSamples(const uint16_t* samples, uint8_t count)
{
	uint16_t sorted[CAL_MEDIAN_SAMPLES] = {0};
	for (uint8_t i = 0; i < count; ++i) {
		sorted[i] = samples[i];
	}

	for (uint8_t i = 1; i < count; ++i) {
		uint16_t value = sorted[i];
		int8_t j = static_cast<int8_t>(i) - 1;
		while (j >= 0 && sorted[j] > value) {
			sorted[j + 1] = sorted[j];
			--j;
		}
		sorted[j + 1] = value;
	}

	return sorted[count / 2];
}

static void calibrationStartCollecting(uint32_t now)
{
	s_calibrationCollecting = true;
	s_calibrationSampleIndex = 0;
	s_calibrationNextSampleMs = now;
}

static bool calibrationCollectOneSample(uint32_t now, const char* phaseLabel, int32_t* outMedianMv)
{
	if (!s_calibrationCollecting) {
		return false;
	}
	if (s_calibrationSampleIndex >= CAL_MEDIAN_SAMPLES) {
		return false;
	}
	if ((int32_t)(now - s_calibrationNextSampleMs) < 0) {
		return false;
	}

	const uint16_t sample = readMoistureMilliVolts();
	s_calibrationSamples[s_calibrationSampleIndex] = sample;
	Serial.printf("CAL: %s sample[%u]=%u mV\n", phaseLabel, s_calibrationSampleIndex, sample);
	s_calibrationSampleIndex++;
	s_calibrationNextSampleMs = now + CAL_SAMPLE_INTERVAL_MS;

	if (s_calibrationSampleIndex < CAL_MEDIAN_SAMPLES) {
		return false;
	}

	const uint16_t median = medianOfSamples(s_calibrationSamples, CAL_MEDIAN_SAMPLES);
	*outMedianMv = median;
	Serial.printf("CAL: %s median=%u mV\n", phaseLabel, median);
	s_calibrationCollecting = false;
	return true;
}

static PressArbEvents processMultipressArbitration(bool shortPress, uint32_t now)
{
	PressArbEvents events{false, false, false};

	if (shortPress) {
		if (s_pressCount < 255) {
			s_pressCount++;
		}
		s_pressWindowDeadlineMs = now + MULTIPRESS_WINDOW_MS;
	}

	if (s_pressCount == 0) {
		return events;
	}
	if ((int32_t)(now - s_pressWindowDeadlineMs) < 0) {
		return events;
	}

	if (s_pressCount >= 5) {
		Serial.printf("BTN_ARB: window closed, count=%u -> debug\n", s_pressCount);
		events.debug = true;
	} else if (s_pressCount == 3) {
		Serial.println("BTN_ARB: window closed, count=3 -> calibration");
		events.triple = true;
	} else if (s_pressCount == 1) {
		Serial.println("BTN_ARB: window closed, count=1 -> single");
		events.single = true;
	} else {
		Serial.printf("BTN_ARB: window closed, count=%u -> ignored\n", s_pressCount);
	}

	s_pressCount = 0;
	return events;
}

static void calibrationTick(uint32_t now, bool rawShortPress, const PressArbEvents& pressEvents, bool longPress)
{
	if (!s_calibrationActive) {
		return;
	}

	const bool anyButtonPress = rawShortPress || longPress || pressEvents.single || pressEvents.triple || pressEvents.debug;
	if (s_calibrationState != CAL_STATE_PROMPT_WET && anyButtonPress) {
		Serial.println("CAL: canceled by button outside prompt stage");
		ledsTriggerOnce(LED_MODE_ERROR_ONCE);
		calibrationExit();
		return;
	}

	if (s_calibrationState == CAL_STATE_MEASURE_DRY_WAIT) {
		if (!s_calibrationCollecting && (now - s_calibrationStateStartedMs) >= CAL_SETTLE_MS) {
			calibrationStartCollecting(now);
		}
		if (calibrationCollectOneSample(now, "dry", &s_calibrationDryMv)) {
			s_calibrationState = CAL_STATE_PROMPT_WET;
			s_calibrationStateStartedMs = now;
			ledsSetBaseMode(LED_MODE_CAL_PROMPT_WET);
			ledsTriggerOnce(LED_MODE_SUCCESS_ONCE);
			Serial.printf("CAL: prompt wet, dry=%ld mV\n", static_cast<long>(s_calibrationDryMv));
		}
		return;
	}

	if (s_calibrationState == CAL_STATE_PROMPT_WET) {
		if (pressEvents.triple) {
			const bool cleared = cal_clear();
			sensorsResetMoistureCalibrationToDefault();
			Serial.printf("CAL: reset requested, nvsClear=%d, defaults restored dry=%ld wet=%ld\n",
										cleared ? 1 : 0,
										static_cast<long>(MOISTURE_DRY_RAW_MV_DEFAULT),
										static_cast<long>(MOISTURE_WET_RAW_MV_DEFAULT));
			ledsTriggerOnce(LED_MODE_CAL_DONE_ONCE);
			calibrationExit();
			return;
		}

		if (pressEvents.single) {
			s_calibrationState = CAL_STATE_MEASURE_WET_WAIT;
			s_calibrationStateStartedMs = now;
			s_calibrationCollecting = false;
			ledsSetBaseMode(LED_MODE_CAL_MEASURE_WET);
			Serial.println("CAL: measure wet start");
			return;
		}

		if ((now - s_calibrationStateStartedMs) >= CAL_PROMPT_TIMEOUT_MS) {
			Serial.println("CAL: prompt timeout, canceled");
			ledsTriggerOnce(LED_MODE_ERROR_ONCE);
			calibrationExit();
		}
		return;
	}

	if (s_calibrationState == CAL_STATE_MEASURE_WET_WAIT) {
		if (!s_calibrationCollecting && (now - s_calibrationStateStartedMs) >= CAL_SETTLE_MS) {
			calibrationStartCollecting(now);
		}
		if (!calibrationCollectOneSample(now, "wet", &s_calibrationWetMv)) {
			return;
		}

		int32_t delta = s_calibrationWetMv - s_calibrationDryMv;
		if (delta < 0) {
			delta = -delta;
		}

		Serial.printf("CAL: dry=%ld wet=%ld delta=%ld mV\n",
									static_cast<long>(s_calibrationDryMv),
									static_cast<long>(s_calibrationWetMv),
									static_cast<long>(delta));

		if (delta < CAL_MIN_DELTA_MV) {
			Serial.println("CAL: delta too small, error");
			ledsTriggerOnce(LED_MODE_ERROR_ONCE);
			calibrationExit();
			return;
		}

		if (!cal_save(s_calibrationDryMv, s_calibrationWetMv)) {
			Serial.println("CAL: save failed");
			ledsTriggerOnce(LED_MODE_ERROR_ONCE);
			calibrationExit();
			return;
		}

		sensorsSetMoistureCalibration(s_calibrationDryMv, s_calibrationWetMv);
		Serial.printf("CAL: saved and applied dry=%ld wet=%ld\n",
									static_cast<long>(s_calibrationDryMv),
									static_cast<long>(s_calibrationWetMv));
		ledsTriggerOnce(LED_MODE_CAL_DONE_ONCE);
		calibrationExit();
	}
}

bool sensorHandleRemoteButtonAction(uint8_t action, uint32_t nowMs)
{
	(void)nowMs;
	if (action == REMOTE_BUTTON_CALIBRATE_START) {
		if (s_calibrationActive) {
			return false;
		}
		s_remoteCalibrateStartPending = true;
		return true;
	}

	if (action == REMOTE_BUTTON_CALIBRATE_MEASURE_WET) {
		if (!s_calibrationActive || s_calibrationState != CAL_STATE_PROMPT_WET) {
			return false;
		}
		s_remoteMeasureWetPending = true;
		return true;
	}

	return false;
}




void setup() {
	Serial.begin(115200);
	delay(1500);

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

	Serial.println("SENSOR: using factory STA MAC");

	if (!espnowInit(ESPNOW_CHANNEL, onRecv, onSend)) {
        Serial.println("espnowInit() failed");
        while (true) { delay(1000); }
    }

	pairingInitNode(ROLE_SENSOR);
	int32_t calDryMv = 0;
	int32_t calWetMv = 0;
	if (cal_load(&calDryMv, &calWetMv)) {
		sensorsSetMoistureCalibration(calDryMv, calWetMv);
		Serial.printf("CAL: loaded from NVS dry=%ld wet=%ld\n", static_cast<long>(calDryMv), static_cast<long>(calWetMv));
	} else {
		sensorsResetMoistureCalibrationToDefault();
		Serial.printf("CAL: defaults dry=%ld wet=%ld\n", static_cast<long>(MOISTURE_DRY_RAW_MV_DEFAULT), static_cast<long>(MOISTURE_WET_RAW_MV_DEFAULT));
	}

	uint8_t restoredHeadMac[6] = {0};
	uint16_t restoredNodeId = 0;
	if (pairingNvsLoadNode(ROLE_SENSOR, restoredHeadMac, &restoredNodeId)) {
		pairingNodeRestorePairedHead(restoredHeadMac, restoredNodeId);
		(void)espnowEnsurePeer(restoredHeadMac, ESPNOW_CHANNEL, false);
		s_autoJoinTriggered = true;
		Serial.printf("PAIRING(NODE): restored pair from NVS nodeId=%u\n", (unsigned)restoredNodeId);
	}
	logStartupCommon("SENSOR", true, pairingNodeIsPaired());
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
	const bool rawShortPress = buttonConsumeShortPress();
	const bool rawLongPress = buttonConsumeLongPress();
	PressArbEvents pressEvents = processMultipressArbitration(rawShortPress, now);

	if (s_remoteCalibrateStartPending) {
		s_remoteCalibrateStartPending = false;
		pressEvents.triple = true;
	}

	if (s_remoteMeasureWetPending) {
		s_remoteMeasureWetPending = false;
		pressEvents.single = true;
	}

	if (!pairStateInitialized) {
		wasPaired = isPaired;
		pairStateInitialized = true;
	}

	if (!s_calibrationActive && pressEvents.triple) {
		Serial.println("CAL: triple detected, entering calibration");
		calibrationEnter(now);
		ledsTick(now);
		return;
	}

	if (s_calibrationActive) {
		calibrationTick(now, rawShortPress, pressEvents, rawLongPress);
		if (s_calibrationActive) {
			ledsTick(now);
			return;
		}
		joinModeActive = pairingNodeIsInJoinMode();
	}

	if (rawLongPress) {
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
		const uint16_t nodeId = pairingNodeId();
		if (pairingNodeHeadMac(headMac)) {
			if (!pairingNvsSaveNode(ROLE_SENSOR, headMac, nodeId)) {
				Serial.println("PAIRING(NODE): NVS save failed");
			}
		}
		Serial.println("PAIRING(NODE): join success");
		ledsTriggerOnce(LED_MODE_SUCCESS_DOUBLE);
	}
	wasPaired = isPaired;

	if (!isPaired && joinModeActive && pairingNodeJoinExpired(now)) {
		pairingNodeExitJoinMode();
		Serial.println("PAIRING(NODE): join window expired");
		ledsTriggerOnce(LED_MODE_ERROR_ONCE);
		joinModeActive = false;
	}

	if (pressEvents.single) {
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

	if (pressEvents.debug || buttonConsumeDebugEnabledEvent()) {
		if (pressEvents.debug) {
			buttonEnableDebug();
		}
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

	sleepLogicSetDebugNoSleep(buttonIsDebugEnabled());
	sleepLogicSetServiceMode(pairingNodeIsInJoinMode() || s_calibrationActive);
	sleepLogicSetIrrigationActive(false);
	telemetryTickSensor(haveMeasurement ? &latestMeasurement : nullptr, haveMeasurement, now);

	uint32_t sleepMs = 0;
	bool deepSleep = false;
	if (sleepLogicShouldEnterSleep(now, &sleepMs, &deepSleep)) {
		Serial.print("SLEEP: entering ");
		Serial.print(deepSleep ? "deep" : "light");
		Serial.print(" sleepMs=");
		Serial.println((unsigned long)sleepMs);
		sleepLogicEnterSleep(sleepMs, deepSleep);
	}
}

#endif


