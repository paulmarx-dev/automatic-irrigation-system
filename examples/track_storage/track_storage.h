#ifndef TRACK_STORAGE_H
#define TRACK_STORAGE_H

#include <Arduino.h>
#include <stddef.h>

// Flags bitfield for FixRec
static constexpr uint8_t FL_CHARGING       = 1u << 0;  // 1 = charging
static constexpr uint8_t FL_GPS_VALID      = 1u << 1;  // 1 = GPS fix valid at sampling time
static constexpr uint8_t FL_MOVE_ACTIVE    = 1u << 2;  // 1 = moving now
static constexpr uint8_t FL_EVT_MOVE_START = 1u << 3;  // edge: start moving
static constexpr uint8_t FL_EVT_MOVE_STOP  = 1u << 4;  // edge: stop moving
static constexpr uint8_t FL_EVT_HEARTBEAT  = 1u << 5;  // heartbeat uplink
static constexpr uint8_t FL_LOW_BATTERY    = 1u << 6;  // bat <= 15%
// bit 7 reserved for future

// GPS fix record
struct FixRec {
  uint32_t seq;     // sequence number
  uint32_t ts;      // epoch seconds if we have it, else millis()/1000
  int32_t  latE7;   // latitude in microdegrees  
  int32_t  lonE7;   // longitude in microdegrees
  uint8_t  bat;     // battery percentage (0-100), 0 if unknown
  uint8_t  flags;   // bitfield: charging, gps_valid, move_active, events, low_bat
};

/**
 * Initialize track storage with given capacity
 * @param capacity Maximum number of FixRec to store
 */
void initTrackStore(size_t capacity);

/**
 * Push a new GPS fix into the ring buffer
 * @param recIn Fix record to push (seq will be auto-assigned)
 * @return true on success, false on mutex failure
 */
bool trackStorePush(FixRec& recIn);

/**
 * Get the latest (most recent) FixRec in storage
 * @param out Output reference to fill with latest record
 * @return true if a record exists, false if storage is empty
 */
bool trackStoreGetLatest(FixRec& out);

/**
 * Get the highest timestamp acknowledged by server
 * @return Acked timestamp (0 if no acks yet)
 */
uint32_t trackStoreGetAckedTs();

/**
 * Set the acked timestamp (server confirmed up to this time)
 * @param ts New acked timestamp
 * @return true on success, false on mutex failure
 */
bool trackStoreSetAckedTs(uint32_t ts);

/**
 * Get the oldest timestamp currently in storage
 * @return Oldest timestamp in buffer (0 if empty)
 */
uint32_t trackStoreGetOldestTs();

/**
 * Get batch of unacked records in order
 * @param outBuf Output buffer for records
 * @param maxN Maximum number of records to retrieve
 * @param afterTs Get records with ts > afterTs (typically ackedTs)
 * @return Number of records copied to outBuf
 */
size_t trackStoreGetBatch(FixRec* outBuf, size_t maxN, uint32_t afterTs);

/**
 * Get current number of records in storage
 * @return Number of valid records currently stored
 */
size_t trackStoreSize();

#endif // TRACK_STORAGE_H
