#pragma once

#include <stdint.h>

#include "protocol.h"

#if defined(DEVICE_ROLE_SENSOR) || defined(DEVICE_ROLE_CONTROL)

enum SleepNodeMode : uint8_t {
  SLEEP_NODE_MODE_OFF = 0,
  SLEEP_NODE_MODE_LIGHT = 1,
  SLEEP_NODE_MODE_DEEP = 5,
};

void sleepLogicInit(bool isControlNode, uint8_t buttonPin, bool buttonActiveLow, SleepNodeMode mode);
void sleepLogicReset();
void sleepLogicSetServiceMode(bool active);
void sleepLogicSetDebugNoSleep(bool enabled);
void sleepLogicSetIrrigationActive(bool enabled);

void sleepLogicOnTelemetrySent(uint16_t seq, uint32_t nowMs);
void sleepLogicOnTelemetryAck(uint16_t ackSeq, uint32_t nowMs);

bool sleepLogicOnSleepPlan(const MsgSleepPlan& plan,
                          uint16_t localNodeId,
                          uint32_t nowMs,
                          MsgSleepAck* outAck);
void sleepLogicOnSleepAckAck(const MsgSleepAckAck& ackAck, uint16_t localNodeId, uint32_t nowMs);

bool sleepLogicBuildRetryAck(uint32_t nowMs, MsgSleepAck* outAck);
void sleepLogicNotifyAckSent(bool sent, uint32_t nowMs);

bool sleepLogicShouldEnterSleep(uint32_t nowMs, uint32_t* outSleepMs, bool* outDeepSleep);
void sleepLogicEnterSleep(uint32_t sleepMs, bool deepSleep);

#endif
