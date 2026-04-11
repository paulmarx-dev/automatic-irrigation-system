#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr uint8_t TRACK_FL_CONTROL = 1u << 0;
static constexpr uint8_t TRACK_FL_LOW_BATTERY_LOCKOUT = 1u << 1;
static constexpr uint8_t TRACK_FL_BATT_EST_VALID = 1u << 2;
static constexpr uint8_t TRACK_FL_CAL_VALID = 1u << 3;
static constexpr uint8_t TRACK_FL_RAW_PRESENT = 1u << 4;
static constexpr uint8_t TRACK_FL_DUPLICATE = 1u << 5;
static constexpr uint8_t TRACK_FL_IRRIGATION_ACTIVE = 1u << 6;

struct TrackRecord {
  uint32_t seq;
  uint32_t tsMs;
  uint16_t nodeId;
  uint16_t telemetrySeq;
  uint16_t moisturePermille;
  uint16_t moistureRawMv;
  uint16_t batteryRawMv;
  uint16_t batteryEstMv;
  uint8_t flags;
  uint8_t mac[6];
};

struct TrackStorageStats {
  size_t capacity;
  size_t size;
  uint32_t totalPushed;
  uint32_t totalOverwritten;
  uint32_t totalDropped;
};

void trackStorageInit(size_t capacity);
bool trackStorageIsReady();
size_t trackStorageCapacity();
size_t trackStorageSize();
bool trackStoragePush(const TrackRecord& recIn);
bool trackStorageGetLatest(TrackRecord* out);
size_t trackStorageCopyWindow(TrackRecord* outBuf, size_t maxN, size_t startOffset);
TrackStorageStats trackStorageGetStats();
