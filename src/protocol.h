#pragma once

#include <stdint.h>

static constexpr uint8_t PROTO_VER = 1;

static constexpr uint8_t MSG_TELEMETRY = 10;
static constexpr uint8_t MSG_TELEMETRY_ACK = 11;
static constexpr uint8_t MSG_REMOTE_BUTTON = 20;

static constexpr uint8_t TELEMETRY_ACK_STATUS_OK = 0;
static constexpr uint8_t TELEMETRY_ACK_STATUS_NOT_PAIRED = 1;

static constexpr uint8_t REMOTE_BUTTON_CALIBRATE_START = 1;
static constexpr uint8_t REMOTE_BUTTON_CALIBRATE_MEASURE_WET = 2;

static constexpr uint8_t FLAG_DIAG_RAW_PRESENT = 0x01;
static constexpr uint8_t FLAG_CAL_VALID = 0x02;
static constexpr uint8_t FLAG_BATT_EST_VALID = 0x04;

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
  uint8_t reserved;
};

#pragma pack(pop)
