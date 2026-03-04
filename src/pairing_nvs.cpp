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
  const bool nodeIdOk = prefs.putUShort(KEY_NODE_ID, nodeId) == nodeId;
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
