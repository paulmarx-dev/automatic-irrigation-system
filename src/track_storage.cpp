#include "track_storage.h"

#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static TrackRecord* s_ring = nullptr;
static size_t s_capacity = 0;
static size_t s_head = 0;
static size_t s_count = 0;
static uint32_t s_nextSeq = 1;
static uint32_t s_totalPushed = 0;
static uint32_t s_totalOverwritten = 0;
static uint32_t s_totalDropped = 0;
static SemaphoreHandle_t s_mtx = nullptr;

void trackStorageInit(size_t capacity)
{
  if (s_mtx == nullptr) {
    s_mtx = xSemaphoreCreateMutex();
    if (s_mtx == nullptr) {
      s_totalDropped++;
      return;
    }
  }

  if (xSemaphoreTake(s_mtx, portMAX_DELAY) != pdTRUE) {
    s_totalDropped++;
    return;
  }

  free(s_ring);
  s_ring = nullptr;
  s_capacity = 0;
  s_head = 0;
  s_count = 0;
  s_nextSeq = 1;
  s_totalPushed = 0;
  s_totalOverwritten = 0;
  s_totalDropped = 0;

  if (capacity > 0) {
    s_ring = static_cast<TrackRecord*>(malloc(sizeof(TrackRecord) * capacity));
    if (s_ring != nullptr) {
      s_capacity = capacity;
      memset(s_ring, 0, sizeof(TrackRecord) * capacity);
    } else {
      s_totalDropped++;
    }
  }

  xSemaphoreGive(s_mtx);
}

bool trackStorageIsReady()
{
  bool ready = false;
  if (s_mtx == nullptr) {
    return false;
  }
  if (xSemaphoreTake(s_mtx, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  ready = (s_ring != nullptr && s_capacity > 0);
  xSemaphoreGive(s_mtx);
  return ready;
}

size_t trackStorageCapacity()
{
  size_t value = 0;
  if (s_mtx == nullptr) {
    return 0;
  }
  if (xSemaphoreTake(s_mtx, portMAX_DELAY) != pdTRUE) {
    return 0;
  }
  value = s_capacity;
  xSemaphoreGive(s_mtx);
  return value;
}

size_t trackStorageSize()
{
  size_t value = 0;
  if (s_mtx == nullptr) {
    return 0;
  }
  if (xSemaphoreTake(s_mtx, portMAX_DELAY) != pdTRUE) {
    return 0;
  }
  value = s_count;
  xSemaphoreGive(s_mtx);
  return value;
}

bool trackStoragePush(const TrackRecord& recIn)
{
  if (s_mtx == nullptr) {
    s_totalDropped++;
    return false;
  }
  if (xSemaphoreTake(s_mtx, portMAX_DELAY) != pdTRUE) {
    s_totalDropped++;
    return false;
  }

  if (s_ring == nullptr || s_capacity == 0) {
    s_totalDropped++;
    xSemaphoreGive(s_mtx);
    return false;
  }

  TrackRecord rec = recIn;
  rec.seq = s_nextSeq++;

  if (s_count == s_capacity) {
    s_totalOverwritten++;
  } else {
    s_count++;
  }

  s_ring[s_head] = rec;
  s_head = (s_head + 1) % s_capacity;
  s_totalPushed++;

  xSemaphoreGive(s_mtx);
  return true;
}

bool trackStorageGetLatest(TrackRecord* out)
{
  if (!out || s_mtx == nullptr) {
    return false;
  }
  if (xSemaphoreTake(s_mtx, portMAX_DELAY) != pdTRUE) {
    return false;
  }

  bool ok = false;
  if (s_ring != nullptr && s_count > 0) {
    const size_t idx = (s_head + s_capacity - 1) % s_capacity;
    *out = s_ring[idx];
    ok = true;
  }

  xSemaphoreGive(s_mtx);
  return ok;
}

size_t trackStorageCopyWindow(TrackRecord* outBuf, size_t maxN, size_t startOffset)
{
  if (!outBuf || maxN == 0 || s_mtx == nullptr) {
    return 0;
  }
  if (xSemaphoreTake(s_mtx, portMAX_DELAY) != pdTRUE) {
    return 0;
  }

  if (s_ring == nullptr || s_count == 0 || startOffset >= s_count) {
    xSemaphoreGive(s_mtx);
    return 0;
  }

  const size_t oldestIdx = (s_head + s_capacity - s_count) % s_capacity;
  size_t idx = (oldestIdx + startOffset) % s_capacity;
  size_t copied = 0;
  const size_t available = s_count - startOffset;
  const size_t limit = (maxN < available) ? maxN : available;

  while (copied < limit) {
    outBuf[copied++] = s_ring[idx];
    idx = (idx + 1) % s_capacity;
  }

  xSemaphoreGive(s_mtx);
  return copied;
}

TrackStorageStats trackStorageGetStats()
{
  TrackStorageStats stats{};
  if (s_mtx == nullptr) {
    return stats;
  }
  if (xSemaphoreTake(s_mtx, portMAX_DELAY) != pdTRUE) {
    return stats;
  }

  stats.capacity = s_capacity;
  stats.size = s_count;
  stats.totalPushed = s_totalPushed;
  stats.totalOverwritten = s_totalOverwritten;
  stats.totalDropped = s_totalDropped;

  xSemaphoreGive(s_mtx);
  return stats;
}
