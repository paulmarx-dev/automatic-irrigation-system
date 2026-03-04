#include "pairing_nvs.h"

#include <Preferences.h>

namespace {

static constexpr const char* NVS_NS_PAIR_NODE = "pair_node";
static constexpr const char* KEY_VER = "ver";
static constexpr const char* KEY_PAIRED = "paired";
static constexpr const char* KEY_HEAD_MAC = "headMac";
static constexpr const char* KEY_NODE_ID = "nodeId";

static constexpr uint8_t SCHEMA_VER = 1;
static constexpr uint8_t PAIRED_TRUE = 1;
static constexpr size_t HEAD_MAC_SIZE = 6;

static constexpr const char* NVS_NS_PAIR_HEAD = "pair_head";
static constexpr const char* KEY_HEAD_BLOB = "blob";

static constexpr uint8_t HEAD_SCHEMA_VER = 1;

struct HeadRegistryBlob {
  uint8_t version;
  uint8_t count;
  uint16_t nextNodeId;
  PairingHeadNodeNvsRecord nodes[PAIRING_NVS_MAX_HEAD_NODES];
  uint32_t crc32;
};

static uint32_t crc32Compute(const uint8_t* data, size_t len)
{
  if (!data || len == 0) {
    return 0;
  }

  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1u)));
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

}  // namespace

bool pairingNvsLoadNode(uint8_t outHeadMac[6], uint16_t* outNodeId)
{
  if (!outHeadMac || !outNodeId) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(NVS_NS_PAIR_NODE, true)) {
    return false;
  }

  const uint8_t ver = prefs.getUChar(KEY_VER, 0);
  if (ver != SCHEMA_VER) {
    prefs.end();
    return false;
  }

  const uint8_t paired = prefs.getUChar(KEY_PAIRED, 0);
  if (paired != PAIRED_TRUE) {
    prefs.end();
    return false;
  }

  const uint16_t nodeId = prefs.getUShort(KEY_NODE_ID, 0);
  if (nodeId == 0) {
    prefs.end();
    return false;
  }

  uint8_t readMac[HEAD_MAC_SIZE] = {0};
  const size_t readLen = prefs.getBytes(KEY_HEAD_MAC, readMac, HEAD_MAC_SIZE);
  prefs.end();

  if (readLen != HEAD_MAC_SIZE) {
    return false;
  }

  for (size_t index = 0; index < HEAD_MAC_SIZE; ++index) {
    outHeadMac[index] = readMac[index];
  }
  *outNodeId = nodeId;
  return true;
}

bool pairingNvsSaveNode(const uint8_t headMac[6], uint16_t nodeId)
{
  if (!headMac || nodeId == 0) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(NVS_NS_PAIR_NODE, false)) {
    return false;
  }

  const bool verOk = prefs.putUChar(KEY_VER, SCHEMA_VER) == 1;
  const bool pairedOk = prefs.putUChar(KEY_PAIRED, PAIRED_TRUE) == 1;
  const bool nodeIdOk = prefs.putUShort(KEY_NODE_ID, nodeId) == sizeof(uint16_t);
  const bool headMacOk = prefs.putBytes(KEY_HEAD_MAC, headMac, HEAD_MAC_SIZE) == HEAD_MAC_SIZE;
  prefs.end();

  return verOk && pairedOk && nodeIdOk && headMacOk;
}

bool pairingNvsClearNode()
{
  Preferences prefs;
  if (!prefs.begin(NVS_NS_PAIR_NODE, false)) {
    return false;
  }

  const bool verOk = prefs.putUChar(KEY_VER, SCHEMA_VER) == 1;
  const bool pairedOk = prefs.putUChar(KEY_PAIRED, 0) == 1;
  const bool hadNodeId = prefs.isKey(KEY_NODE_ID);
  const bool nodeIdOk = !hadNodeId || prefs.remove(KEY_NODE_ID);
  const bool hadHeadMac = prefs.isKey(KEY_HEAD_MAC);
  const bool headMacOk = !hadHeadMac || prefs.remove(KEY_HEAD_MAC);
  prefs.end();

  return verOk && pairedOk && nodeIdOk && headMacOk;
}

bool pairingNvsLoadHead(PairingHeadNodeNvsRecord outNodes[PAIRING_NVS_MAX_HEAD_NODES],
                        uint8_t* outCount,
                        uint16_t* outNextNodeId)
{
  if (!outNodes || !outCount || !outNextNodeId) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(NVS_NS_PAIR_HEAD, true)) {
    return false;
  }

  // Head registry is persisted as one blob to avoid partial multi-key writes.
  HeadRegistryBlob blob{};
  const size_t blobLen = prefs.getBytes(KEY_HEAD_BLOB, &blob, sizeof(blob));
  if (blobLen == sizeof(blob) &&
      blob.version == HEAD_SCHEMA_VER &&
      blob.count <= PAIRING_NVS_MAX_HEAD_NODES &&
      blob.nextNodeId != 0) {
    const uint32_t expectedCrc = crc32Compute(reinterpret_cast<const uint8_t*>(&blob), sizeof(blob) - sizeof(blob.crc32));
    if (expectedCrc == blob.crc32) {
      for (uint8_t i = 0; i < PAIRING_NVS_MAX_HEAD_NODES; ++i) {
        outNodes[i] = blob.nodes[i];
      }
      *outCount = blob.count;
      *outNextNodeId = blob.nextNodeId;
      prefs.end();
      return true;
    }
  }
  prefs.end();
  return false;
}

bool pairingNvsSaveHead(const PairingHeadNodeNvsRecord nodes[PAIRING_NVS_MAX_HEAD_NODES],
                        uint8_t count,
                        uint16_t nextNodeId)
{
  if (!nodes || count > PAIRING_NVS_MAX_HEAD_NODES || nextNodeId == 0) {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(NVS_NS_PAIR_HEAD, false)) {
    return false;
  }

  HeadRegistryBlob blob{};
  blob.version = HEAD_SCHEMA_VER;
  blob.count = count;
  blob.nextNodeId = nextNodeId;
  for (uint8_t i = 0; i < PAIRING_NVS_MAX_HEAD_NODES; ++i) {
    blob.nodes[i] = nodes[i];
  }
  // CRC is stored with payload to detect torn/corrupted writes on load.
  blob.crc32 = crc32Compute(reinterpret_cast<const uint8_t*>(&blob), sizeof(blob) - sizeof(blob.crc32));

  const bool blobOk = prefs.putBytes(KEY_HEAD_BLOB, &blob, sizeof(blob)) == sizeof(blob);
  prefs.end();

  return blobOk;
}

bool pairingNvsClearHead()
{
  Preferences prefs;
  if (!prefs.begin(NVS_NS_PAIR_HEAD, false)) {
    return false;
  }

  const bool hadBlob = prefs.isKey(KEY_HEAD_BLOB);

  const bool blobOk = !hadBlob || prefs.remove(KEY_HEAD_BLOB);
  prefs.end();

  return blobOk;
}

/*
  Namespace plan:
  - Node pairing (implemented): pair_node
  - Node calibration (future): cal_node
  - Head registry (future): pair_head

  Head registry concept (future, not implemented):
  - pair_head keeps fixed slots slot0..slotN
  - slot0 is reserved for Control Unit role
  - sensors use remaining slots
*/
