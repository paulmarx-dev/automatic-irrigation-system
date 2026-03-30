#pragma once

#include <stdint.h>

static constexpr uint8_t PROTO_VER = 1;

static constexpr uint8_t MSG_TELEMETRY = 10;
static constexpr uint8_t MSG_TELEMETRY_ACK = 11;
static constexpr uint8_t MSG_REMOTE_BUTTON = 20;
static constexpr uint8_t MSG_IRRIGATION_STATE = 21;
static constexpr uint8_t MSG_COMMAND_ACK = 22;
static constexpr uint8_t MSG_SLEEP_PLAN = 30;
static constexpr uint8_t MSG_SLEEP_ACK = 31;
static constexpr uint8_t MSG_SLEEP_ACK_ACK = 32;
static constexpr uint8_t MSG_CRITICAL_SLEEP_INTENT = 33;
static constexpr uint8_t MSG_CRITICAL_SLEEP_ACK = 34;

static constexpr uint8_t TELEMETRY_ACK_STATUS_OK = 0;
static constexpr uint8_t TELEMETRY_ACK_STATUS_NOT_PAIRED = 1;

static constexpr uint8_t REMOTE_BUTTON_CALIBRATE_START = 1;
static constexpr uint8_t REMOTE_BUTTON_CALIBRATE_MEASURE_WET = 2;
static constexpr uint8_t REMOTE_BUTTON_IRRIGATION_START = 3;
static constexpr uint8_t REMOTE_BUTTON_IRRIGATION_STOP = 4;
static constexpr uint8_t REMOTE_BUTTON_IRRIGATION_STATE_REQUEST = 5;

static constexpr uint8_t COMMAND_ACK_STATUS_RECEIVED = 1;
static constexpr uint8_t COMMAND_ACK_STATUS_APPLIED = 2;
static constexpr uint8_t COMMAND_ACK_STATUS_REJECTED = 3;

static constexpr uint8_t SLEEP_ACK_REJECT_NONE = 0;
static constexpr uint8_t SLEEP_ACK_REJECT_BUSY = 1;
static constexpr uint8_t SLEEP_ACK_REJECT_EXPIRED = 2;
static constexpr uint8_t SLEEP_ACK_REJECT_WRONG_NODE = 3;

static constexpr uint8_t CRITICAL_SLEEP_REASON_LOW_BATTERY = 1;
static constexpr uint8_t CRITICAL_SLEEP_ACK_STATUS_OK = 0;
static constexpr uint8_t CRITICAL_SLEEP_ACK_STATUS_REJECTED = 1;

static constexpr uint8_t FLAG_DIAG_RAW_PRESENT = 0x01;
static constexpr uint8_t FLAG_CAL_VALID = 0x02;
static constexpr uint8_t FLAG_BATT_EST_VALID = 0x04;
static constexpr uint8_t FLAG_NODE_ROLE_CONTROL = 0x08;
static constexpr uint8_t FLAG_NODE_LOW_BATTERY_LOCKOUT = 0x10;
static constexpr uint8_t FLAG_IRRIGATION_ACTIVE = 0x20;

static constexpr uint8_t IRRIGATION_STATE_OFF = 0;
static constexpr uint8_t IRRIGATION_STATE_RUN = 1;

#pragma pack(push, 1)

struct MsgHdr {
  uint8_t ver;
  uint8_t type;
  uint16_t seq;
  uint16_t nodeId;
};

struct MsgTelemetry {
  MsgHdr hdr;
  uint16_t moisturePermille;  // 0..1000 == 0.0..100.0%
  uint16_t moistureRawMv;     // raw moisture value in mV (extension for diagnostics)
  uint16_t batteryRawMv;      // battery reading at ADC pin in mV
  uint16_t batteryEstMv;      // estimated battery voltage after divider/correction in mV
  uint8_t flags;
  uint8_t reserved;
};

struct MsgTelemetryAck {
  MsgHdr hdr;
  uint16_t ackSeq;
  uint8_t status;
  uint8_t reserved;
};

struct MsgRemoteButton {
  MsgHdr hdr;
  uint8_t action;
  uint16_t cmdId;
};

struct MsgIrrigationState {
  MsgHdr hdr;
  uint8_t desiredState;
  uint8_t reserved;
  uint64_t leaseId;
  uint32_t remainingLeaseMs;
};

struct MsgCommandAck {
  MsgHdr hdr;
  uint16_t cmdId;
  uint8_t action;
  uint8_t status;
  uint8_t irrigationState;
  uint8_t reserved;
  uint32_t remainingSec;
};

struct MsgSleepPlan {
  MsgHdr hdr;
  uint32_t planId;
  uint32_t headBootId;
  uint32_t sleepMs;
  uint32_t validUntilMs;
  uint16_t baseSleepSec;
  uint16_t slotDelayMs;
};

struct MsgSleepAck {
  MsgHdr hdr;
  uint32_t planId;
  uint32_t headBootId;
  uint8_t accepted;
  uint8_t rejectReason;
  uint16_t reserved;
  uint32_t effectiveSleepMs;
};

struct MsgSleepAckAck {
  MsgHdr hdr;
  uint32_t planId;
  uint32_t headBootId;
  uint8_t commit;
  uint8_t reserved[3];
};

struct MsgCriticalSleepIntent {
  MsgHdr hdr;
  uint8_t reason;
  uint8_t flags;
  uint16_t thresholdMv;
  uint16_t batteryEstMv;
  uint16_t reserved;
};

struct MsgCriticalSleepAck {
  MsgHdr hdr;
  uint16_t ackSeq;
  uint8_t status;
  uint8_t reserved;
};

#pragma pack(pop)
