#include "track_storage.h"
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Ring storage for GPS fixes
static std::vector<FixRec> ring;    // the ring storage for GPS fixes, allocated to max capacity in initTrackStorage()
static size_t cap = 0;              // storage capacity (number of records) - set by initTrackStorage()
static size_t head = 0;             // next write position
static size_t count = 0;            // how many valid records in the ring (<= cap)
static uint32_t nextSeq = 1;        // next sequence number to assign to new record
static uint32_t ackedTs = 0;        // highest timestamp confirmed by server (ACK)

static SemaphoreHandle_t mtx;       // mutex to protect concurrent access to ring buffer and related variables

void initTrackStore(size_t capacity) {
  cap = capacity;
  ring.assign(capacity, FixRec{});  // pre-allocate storage
  head = 0;
  count = 0;
  nextSeq = 1;
  ackedTs = 0;
  mtx = xSemaphoreCreateMutex();
}

bool trackStorePush(FixRec& recIn) {
  if (xSemaphoreTake(mtx, portMAX_DELAY) != pdTRUE) {
    return false; // failed to acquire mutex
  }

  FixRec& rec = recIn;
  rec.seq = nextSeq++;

  // If full, we overwrite the oldest (which is at head when count==cap).
  // Oldest seq in buffer is not tracked explicitly here
  ring[head] = rec;
  head = (head + 1) % cap;
  if (count < cap) { 
    count++; 
  }
  
  xSemaphoreGive(mtx);
  return true;
}

uint32_t trackStoreGetAckedTs() {
  uint32_t ts = 0;
  if (xSemaphoreTake(mtx, portMAX_DELAY) != pdTRUE) {
    return 0; // failed to acquire mutex
  }
  ts = ackedTs;
  xSemaphoreGive(mtx);
  return ts;
}

bool trackStoreSetAckedTs(uint32_t ts) {
  if (xSemaphoreTake(mtx, portMAX_DELAY) != pdTRUE) {
    return false; // failed to acquire mutex
  }
  if (ts > ackedTs) {
    ackedTs = ts;
  }
  xSemaphoreGive(mtx);
  return true;
}

uint32_t trackStoreGetOldestTs() {
  uint32_t ts = 0;
  if (xSemaphoreTake(mtx, portMAX_DELAY) != pdTRUE) {
    return 0; // failed to acquire mutex
  }
  if (count == 0) {
    ts = 0;
  } else {
    // Calculate oldest index: head points to next write, so oldest is count positions back
    size_t oldestIdx = (head + cap - count) % cap;
    ts = ring[oldestIdx].ts;
  }
  xSemaphoreGive(mtx);
  return ts;
}

bool trackStoreGetLatest(FixRec& out) {
  if (xSemaphoreTake(mtx, portMAX_DELAY) != pdTRUE) {
    return false; // failed to acquire mutex
  }

  bool found = false;
  if (count > 0) {
    // Latest record is at (head - 1 + cap) % cap
    size_t latestIdx = (head + cap - 1) % cap;
    out = ring[latestIdx];
    found = true;
  }

  xSemaphoreGive(mtx);
  return found;
}

// Returns records with ts > afterTs (typically afterTs == ackedTs)
// Copies up to maxN in increasing seq order.
size_t trackStoreGetBatch(FixRec* outBuf, size_t maxN, uint32_t afterTs) {
  if (!outBuf || maxN == 0) return 0;
  if (xSemaphoreTake(mtx, portMAX_DELAY) != pdTRUE) {
    return 0; // failed to acquire mutex
  }

  size_t n = 0;
  
  if (count == 0) {
    n = 0;
  } else {
    size_t oldestIdx = (head + cap - count) % cap;
    size_t idx = oldestIdx;
    for (size_t i = 0; i < count && n < maxN; i++) {
      if (ring[idx].ts > afterTs) {
        outBuf[n++] = ring[idx];
      }
      idx = (idx + 1) % cap;
    }
  }

  xSemaphoreGive(mtx);
  return n;
}

size_t trackStoreSize() {
  size_t c = 0;
  if (xSemaphoreTake(mtx, portMAX_DELAY) != pdTRUE) {
    return 0; // failed to acquire mutex
  }
  c = count;
  xSemaphoreGive(mtx);
  return c;
}