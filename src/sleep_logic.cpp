#include "sleep_logic.h"

#if defined(DEVICE_ROLE_SENSOR) || defined(DEVICE_ROLE_CONTROL)

#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <driver/gpio.h>

#include "common_config.h"

namespace {

struct SleepPlanState {
  bool valid;
  bool telemetryAcked;
  bool waitingAckAck;
  bool sleepCommitted;
  bool ackInFlight;
  bool isControlNode;
  bool serviceModeActive;
  bool debugNoSleep;
  bool irrigationActive;
  uint8_t buttonPin;
  bool buttonActiveLow;
  SleepNodeMode mode;
  uint16_t lastTelemetrySeq;
  uint32_t planId;
  uint32_t headBootId;
  uint32_t sleepMs;
  uint32_t lastPlanAtMs;
  uint32_t nextAckRetryAtMs;
  uint32_t ackAckDeadlineMs;
  uint8_t ackRetries;
  MsgSleepAck pendingAck;
};

static SleepPlanState s_state = {};

static uint32_t boundedRandomMs(uint32_t maxExclusive)
{
  if (maxExclusive == 0) {
    return 0;
  }
  return static_cast<uint32_t>(esp_random() % maxExclusive);
}

static bool sleepSuppressed()
{
  if (s_state.debugNoSleep || s_state.serviceModeActive) {
    return true;
  }
  if (s_state.isControlNode && s_state.irrigationActive) {
    return true;
  }
  return false;
}

static void clearPlanState()
{
  s_state.valid = false;
  s_state.waitingAckAck = false;
  s_state.sleepCommitted = false;
  s_state.ackInFlight = false;
  s_state.planId = 0;
  s_state.headBootId = 0;
  s_state.sleepMs = 0;
  s_state.lastPlanAtMs = 0;
  s_state.nextAckRetryAtMs = 0;
  s_state.ackAckDeadlineMs = 0;
  s_state.ackRetries = 0;
  memset(&s_state.pendingAck, 0, sizeof(s_state.pendingAck));
}

}  // namespace

void sleepLogicInit(bool isControlNode, uint8_t buttonPin, bool buttonActiveLow, SleepNodeMode mode)
{
  memset(&s_state, 0, sizeof(s_state));
  s_state.isControlNode = isControlNode;
  s_state.buttonPin = buttonPin;
  s_state.buttonActiveLow = buttonActiveLow;
  s_state.mode = mode;
}

void sleepLogicReset()
{
  s_state.telemetryAcked = false;
  s_state.lastTelemetrySeq = 0;
  clearPlanState();
}

void sleepLogicSetServiceMode(bool active)
{
  s_state.serviceModeActive = active;
  if (active) {
    clearPlanState();
  }
}

void sleepLogicSetDebugNoSleep(bool enabled)
{
  s_state.debugNoSleep = enabled;
  if (enabled) {
    clearPlanState();
  }
}

void sleepLogicSetIrrigationActive(bool enabled)
{
  s_state.irrigationActive = enabled;
}

void sleepLogicOnTelemetrySent(uint16_t seq, uint32_t nowMs)
{
  (void)nowMs;
  s_state.lastTelemetrySeq = seq;
  s_state.telemetryAcked = false;
  clearPlanState();
}

void sleepLogicOnTelemetryAck(uint16_t ackSeq, uint32_t nowMs)
{
  (void)nowMs;
  if (ackSeq == s_state.lastTelemetrySeq) {
    s_state.telemetryAcked = true;
  }
}

bool sleepLogicOnSleepPlan(const MsgSleepPlan& plan,
                          uint16_t localNodeId,
                          uint32_t nowMs,
                          MsgSleepAck* outAck)
{
  if (!outAck) {
    return false;
  }

  memset(outAck, 0, sizeof(*outAck));
  outAck->hdr.ver = PROTO_VER;
  outAck->hdr.type = MSG_SLEEP_ACK;
  outAck->hdr.seq = plan.hdr.seq;
  outAck->hdr.nodeId = localNodeId;
  outAck->planId = plan.planId;
  outAck->headBootId = plan.headBootId;

  if (plan.hdr.nodeId != 0 && plan.hdr.nodeId != localNodeId) {
    outAck->accepted = 0;
    outAck->rejectReason = SLEEP_ACK_REJECT_WRONG_NODE;
    return true;
  }

  if (sleepSuppressed()) {
    outAck->accepted = 0;
    outAck->rejectReason = SLEEP_ACK_REJECT_BUSY;
    return true;
  }

  if ((int32_t)(plan.validUntilMs - nowMs) <= 0) {
    outAck->accepted = 0;
    outAck->rejectReason = SLEEP_ACK_REJECT_EXPIRED;
    return true;
  }

  outAck->accepted = 1;
  outAck->rejectReason = SLEEP_ACK_REJECT_NONE;
  outAck->effectiveSleepMs = plan.sleepMs;

  s_state.valid = true;
  s_state.waitingAckAck = true;
  s_state.sleepCommitted = false;
  s_state.ackInFlight = true;
  s_state.planId = plan.planId;
  s_state.headBootId = plan.headBootId;
  s_state.sleepMs = plan.sleepMs;
  s_state.lastPlanAtMs = nowMs;
  s_state.ackRetries = 0;
  s_state.ackAckDeadlineMs = nowMs + WAIT_ACK_ACK_TIMEOUT_MS;
  s_state.nextAckRetryAtMs = nowMs + SLEEP_ACK_RETRY_MIN_MS + boundedRandomMs(SLEEP_ACK_RETRY_JITTER_MS + 1);
  s_state.pendingAck = *outAck;
  return true;
}

void sleepLogicOnSleepAckAck(const MsgSleepAckAck& ackAck, uint16_t localNodeId, uint32_t nowMs)
{
  (void)nowMs;
  if (!s_state.waitingAckAck || !s_state.valid) {
    return;
  }
  if (ackAck.hdr.nodeId != 0 && ackAck.hdr.nodeId != localNodeId) {
    return;
  }
  if (ackAck.planId != s_state.planId || ackAck.headBootId != s_state.headBootId) {
    return;
  }
  if (!ackAck.commit) {
    return;
  }

  s_state.waitingAckAck = false;
  s_state.ackInFlight = false;
  s_state.sleepCommitted = true;
}

bool sleepLogicBuildRetryAck(uint32_t nowMs, MsgSleepAck* outAck)
{
  if (!outAck || !s_state.waitingAckAck || !s_state.valid) {
    return false;
  }

  if ((int32_t)(nowMs - s_state.ackAckDeadlineMs) < 0) {
    return false;
  }

  if (s_state.ackRetries >= SLEEP_ACK_RETRY_MAX) {
    clearPlanState();
    return false;
  }

  if ((int32_t)(nowMs - s_state.nextAckRetryAtMs) < 0) {
    return false;
  }

  *outAck = s_state.pendingAck;
  s_state.ackInFlight = true;
  s_state.ackRetries++;
  s_state.nextAckRetryAtMs = nowMs + SLEEP_ACK_RETRY_MIN_MS + boundedRandomMs(SLEEP_ACK_RETRY_JITTER_MS + 1);
  s_state.ackAckDeadlineMs = nowMs + WAIT_ACK_ACK_TIMEOUT_MS;
  return true;
}

void sleepLogicNotifyAckSent(bool sent, uint32_t nowMs)
{
  if (!s_state.waitingAckAck) {
    return;
  }
  if (!sent) {
    s_state.ackInFlight = false;
    s_state.nextAckRetryAtMs = nowMs + SLEEP_ACK_RETRY_MIN_MS + boundedRandomMs(SLEEP_ACK_RETRY_JITTER_MS + 1);
    return;
  }
  s_state.ackInFlight = false;
}

bool sleepLogicShouldEnterSleep(uint32_t nowMs, uint32_t* outSleepMs, bool* outDeepSleep)
{
  (void)nowMs;
  if (!outSleepMs || !outDeepSleep) {
    return false;
  }
  if (!s_state.sleepCommitted || !s_state.valid) {
    return false;
  }
  if (sleepSuppressed()) {
    return false;
  }
  if (s_state.mode == SLEEP_NODE_MODE_OFF) {
    return false;
  }

  *outSleepMs = s_state.sleepMs;
  *outDeepSleep = (s_state.mode == SLEEP_NODE_MODE_DEEP);
  clearPlanState();
  s_state.telemetryAcked = false;
  return true;
}

void sleepLogicEnterSleep(uint32_t sleepMs, bool deepSleep)
{
  if (sleepMs == 0) {
    return;
  }

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  const uint64_t sleepUs = static_cast<uint64_t>(sleepMs) * 1000ULL;
  esp_sleep_enable_timer_wakeup(sleepUs);

  if (s_state.buttonPin != 255) {
    const gpio_int_type_t wakeType = s_state.buttonActiveLow ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL;
    (void)gpio_wakeup_enable(static_cast<gpio_num_t>(s_state.buttonPin), wakeType);
    (void)esp_sleep_enable_gpio_wakeup();
  }

  if (deepSleep) {
    esp_deep_sleep_start();
    return;
  }

  esp_light_sleep_start();
}

#endif
