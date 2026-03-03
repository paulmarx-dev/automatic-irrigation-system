#include "cal_nvs.h"

#include <Preferences.h>

namespace {

static constexpr const char* NVS_NS_CAL_NODE = "cal_node";
static constexpr const char* KEY_CAL = "cal";

static constexpr uint8_t CAL_SCHEMA_VER = 1;
static constexpr uint8_t CAL_VALID_TRUE = 1;

struct CalRecord {
  uint8_t ver;
  uint8_t valid;
  int32_t dryMv;
  int32_t wetMv;
  uint32_t checksum;
};

static uint32_t calChecksum(const CalRecord& record)
{
  uint32_t hash = 2166136261u;
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  const size_t count = sizeof(CalRecord) - sizeof(record.checksum);
  for (size_t index = 0; index < count; ++index) {
    hash ^= bytes[index];
    hash *= 16777619u;
  }
  return hash;
}

}  // namespace

bool cal_load(int32_t* outDryMv, int32_t* outWetMv)
{
  if (!outDryMv || !outWetMv) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(NVS_NS_CAL_NODE, true)) {
    return false;
  }

  CalRecord record{};
  const size_t len = prefs.getBytes(KEY_CAL, &record, sizeof(record));
  prefs.end();

  if (len != sizeof(record)) {
    return false;
  }
  if (record.ver != CAL_SCHEMA_VER || record.valid != CAL_VALID_TRUE) {
    return false;
  }
  if (record.checksum != calChecksum(record)) {
    return false;
  }

  *outDryMv = record.dryMv;
  *outWetMv = record.wetMv;
  return true;
}

bool cal_save(int32_t dryMv, int32_t wetMv)
{
  Preferences prefs;
  if (!prefs.begin(NVS_NS_CAL_NODE, false)) {
    return false;
  }

  CalRecord record{};
  record.ver = CAL_SCHEMA_VER;
  record.valid = CAL_VALID_TRUE;
  record.dryMv = dryMv;
  record.wetMv = wetMv;
  record.checksum = calChecksum(record);

  const size_t written = prefs.putBytes(KEY_CAL, &record, sizeof(record));
  prefs.end();

  return written == sizeof(record);
}

bool cal_clear()
{
  Preferences prefs;
  if (!prefs.begin(NVS_NS_CAL_NODE, false)) {
    return false;
  }

  const bool hadKey = prefs.isKey(KEY_CAL);
  const bool ok = !hadKey || prefs.remove(KEY_CAL);
  prefs.end();

  return ok;
}
