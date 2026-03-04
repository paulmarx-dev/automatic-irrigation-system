#include "pairing.h"
#include "esp_now_helpers.h"
#include "common_config.h"
#include "pairing_nvs.h"

#include <Arduino.h>
#include <string.h>

#include <esp_mac.h>

static const uint8_t ESPNOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static bool s_isHead = false;
static bool s_isNode = false;

static uint8_t s_headId = 1;
static uint8_t s_nodeRole = ROLE_SENSOR;

static uint8_t s_localFactoryUid[6] = {0};

static uint32_t s_headSessionId = 0;
static uint32_t s_nodeSessionId = 0;

static uint32_t s_lastBeaconMs = 0;
static uint32_t s_beaconTxCount = 0;
static uint32_t s_beaconRxCount = 0;

static uint16_t s_nextNodeId = 1;

static uint16_t s_pendingNodeId = 0;
static uint32_t s_pendingSessionId = 0;
static uint8_t s_pendingNodeUid[6] = {0};
static uint8_t s_pendingNodeMac[6] = {0};

static constexpr uint8_t MAX_HEAD_PAIRED_NODES = 8;

struct HeadPairedNode {
  bool used;
  uint16_t nodeId;
  uint8_t mac[6];
  uint8_t uid[6];
};

static HeadPairedNode s_headPairedNodes[MAX_HEAD_PAIRED_NODES] = {};
static uint8_t s_headPairedCount = 0;

static bool s_headPaired = false;
static uint16_t s_headPairedNodeId = 0;
static uint8_t s_headPairedNodeMac[6] = {0};
static uint8_t s_headPairedNodeUid[6] = {0};
static bool s_headPairSuccessEvent = false;
static bool s_headOpen = false;
static uint32_t s_headOpenDeadlineMs = 0;
static bool s_headRebindArmed = false;
static bool s_headCandidateFilterActive = false;
static uint8_t s_headCandidateMac[6] = {0};
static uint32_t s_headCandidateLastOpenMs = 0;

static bool s_headNvsDirty = false;
static uint32_t s_headNvsSaveDueMs = 0;
static uint8_t s_headNvsRetryCount = 0;
static constexpr uint32_t HEAD_NVS_SAVE_DELAY_MS = 400;
static constexpr uint32_t HEAD_NVS_RETRY_BASE_MS = 500;
static constexpr uint8_t HEAD_NVS_RETRY_MAX = 4;

static bool s_nodePaired = false;
static uint16_t s_nodeId = 0;
static uint8_t s_nodeHeadMac[6] = {0};
static bool s_nodeInJoinMode = false;
static uint32_t s_nodeJoinDeadlineMs = 0;

static bool s_seenHead = false;
static bool s_multiHeadConflict = false;
static uint8_t s_seenHeadMac[6] = {0};

static bool s_joinSent = false;
static uint32_t s_lastJoinMs = 0;
static uint16_t s_offerNodeId = 0;
static bool s_offerReceived = false;

static int8_t headFindPairedSlotByNodeIdMac(uint16_t nodeId, const uint8_t mac[6])
{
  for (uint8_t index = 0; index < MAX_HEAD_PAIRED_NODES; ++index) {
    if (!s_headPairedNodes[index].used) {
      continue;
    }
    if (s_headPairedNodes[index].nodeId == nodeId && memcmp(s_headPairedNodes[index].mac, mac, 6) == 0) {
      return static_cast<int8_t>(index);
    }
  }
  return -1;
}

static int8_t headFindPairedSlotByMac(const uint8_t mac[6])
{
  for (uint8_t index = 0; index < MAX_HEAD_PAIRED_NODES; ++index) {
    if (!s_headPairedNodes[index].used) {
      continue;
    }
    if (memcmp(s_headPairedNodes[index].mac, mac, 6) == 0) {
      return static_cast<int8_t>(index);
    }
  }
  return -1;
}

static int8_t headFindPairedSlotByUid(const uint8_t uid[6])
{
  for (uint8_t index = 0; index < MAX_HEAD_PAIRED_NODES; ++index) {
    if (!s_headPairedNodes[index].used) {
      continue;
    }
    if (memcmp(s_headPairedNodes[index].uid, uid, 6) == 0) {
      return static_cast<int8_t>(index);
    }
  }
  return -1;
}

static int8_t headFindFreePairedSlot()
{
  for (uint8_t index = 0; index < MAX_HEAD_PAIRED_NODES; ++index) {
    if (!s_headPairedNodes[index].used) {
      return static_cast<int8_t>(index);
    }
  }
  return -1;
}

static void headClearPairedRegistry()
{
  memset(s_headPairedNodes, 0, sizeof(s_headPairedNodes));
  s_headPairedCount = 0;
}

static bool headPersistPairedRegistryNow()
{
  PairingHeadNodeNvsRecord records[PAIRING_NVS_MAX_HEAD_NODES] = {};
  for (uint8_t index = 0; index < MAX_HEAD_PAIRED_NODES; ++index) {
    if (!s_headPairedNodes[index].used) {
      continue;
    }
    records[index].nodeId = s_headPairedNodes[index].nodeId;
    memcpy(records[index].mac, s_headPairedNodes[index].mac, 6);
    memcpy(records[index].uid, s_headPairedNodes[index].uid, 6);
  }

  return pairingNvsSaveHead(records, s_headPairedCount, s_nextNodeId);
}

static void headSchedulePersist(uint32_t nowMs)
{
  s_headNvsDirty = true;
  s_headNvsSaveDueMs = nowMs + HEAD_NVS_SAVE_DELAY_MS;
}

static void headPersistTick(uint32_t nowMs)
{
  if (!s_headNvsDirty) {
    return;
  }

  if ((int32_t)(nowMs - s_headNvsSaveDueMs) < 0) {
    return;
  }

  if (headPersistPairedRegistryNow()) {
    s_headNvsDirty = false;
    s_headNvsSaveDueMs = 0;
    s_headNvsRetryCount = 0;
    return;
  }

  if (s_headNvsRetryCount < HEAD_NVS_RETRY_MAX) {
    s_headNvsRetryCount++;
  }

  const uint8_t backoffPow = s_headNvsRetryCount > 3 ? 3 : s_headNvsRetryCount;
  const uint32_t backoffMs = HEAD_NVS_RETRY_BASE_MS << backoffPow;
  s_headNvsSaveDueMs = nowMs + backoffMs;

  Serial.print("PAIRING(HEAD): NVS save failed, retry in ms=");
  Serial.println((unsigned long)backoffMs);
}

static void headRestorePairedRegistry()
{
  PairingHeadNodeNvsRecord records[PAIRING_NVS_MAX_HEAD_NODES] = {};
  uint8_t count = 0;
  uint16_t nextNodeId = 1;
  if (!pairingNvsLoadHead(records, &count, &nextNodeId)) {
    return;
  }

  headClearPairedRegistry();
  uint16_t maxNodeId = 0;
  uint8_t restoredCount = 0;
  for (uint8_t index = 0; index < MAX_HEAD_PAIRED_NODES; ++index) {
    if (records[index].nodeId == 0) {
      continue;
    }
    s_headPairedNodes[index].used = true;
    s_headPairedNodes[index].nodeId = records[index].nodeId;
    memcpy(s_headPairedNodes[index].mac, records[index].mac, 6);
    memcpy(s_headPairedNodes[index].uid, records[index].uid, 6);
    restoredCount++;
    if (records[index].nodeId > maxNodeId) {
      maxNodeId = records[index].nodeId;
    }
  }

  s_headPairedCount = restoredCount;
  if (s_headPairedCount > 0) {
    s_headPaired = true;
    for (uint8_t index = 0; index < MAX_HEAD_PAIRED_NODES; ++index) {
      if (!s_headPairedNodes[index].used) {
        continue;
      }
      s_headPairedNodeId = s_headPairedNodes[index].nodeId;
      memcpy(s_headPairedNodeMac, s_headPairedNodes[index].mac, 6);
      memcpy(s_headPairedNodeUid, s_headPairedNodes[index].uid, 6);
      break;
    }
  }

  uint16_t fallbackNext = maxNodeId > 0 ? static_cast<uint16_t>(maxNodeId + 1) : 1;
  s_nextNodeId = nextNodeId > fallbackNext ? nextNodeId : fallbackNext;

  Serial.print("PAIRING(HEAD): restored paired nodes from NVS count=");
  Serial.print((unsigned long)s_headPairedCount);
  Serial.print(" storedCount=");
  Serial.println((unsigned long)count);
}

static bool headUpsertPairedNode(uint16_t nodeId, const uint8_t mac[6], const uint8_t uid[6])
{
  int8_t slot = headFindPairedSlotByNodeIdMac(nodeId, mac);
  if (slot < 0) {
    slot = headFindPairedSlotByMac(mac);
  }
  if (slot < 0) {
    slot = headFindPairedSlotByUid(uid);
  }
  if (slot < 0) {
    slot = headFindFreePairedSlot();
  }
  if (slot < 0) {
    return false;
  }

  HeadPairedNode* entry = &s_headPairedNodes[slot];
  const bool wasUsed = entry->used;
  const bool unchanged = wasUsed &&
                         entry->nodeId == nodeId &&
                         memcmp(entry->mac, mac, 6) == 0 &&
                         memcmp(entry->uid, uid, 6) == 0;
  if (unchanged) {
    return false;
  }

  entry->used = true;
  entry->nodeId = nodeId;
  memcpy(entry->mac, mac, 6);
  memcpy(entry->uid, uid, 6);

  if (!wasUsed && s_headPairedCount < 255) {
    s_headPairedCount++;
  }

  return true;
}

void pairingHeadSetOpen(bool open)
{
  s_headOpen = open;
  if (open) {
    s_headOpenDeadlineMs = millis() + PAIRING_HEAD_OPEN_MS;
    s_headRebindArmed = s_headPaired;
    s_headCandidateFilterActive = false;
    memset(s_headCandidateMac, 0, sizeof(s_headCandidateMac));
  } else {
    s_headOpenDeadlineMs = 0;
    s_headRebindArmed = false;
    s_headCandidateFilterActive = false;
    memset(s_headCandidateMac, 0, sizeof(s_headCandidateMac));
  }
}

bool pairingHeadOpenCandidateWindow(const uint8_t candidateMac[6], uint32_t nowMs, uint32_t openMs, uint32_t cooldownMs)
{
  if (!candidateMac || openMs == 0) {
    return false;
  }

  if (cooldownMs > 0 &&
      s_headCandidateLastOpenMs != 0 &&
      (int32_t)(nowMs - s_headCandidateLastOpenMs) < (int32_t)cooldownMs) {
    return false;
  }

  s_headOpen = true;
  s_headOpenDeadlineMs = nowMs + openMs;
  s_headRebindArmed = false;
  s_headCandidateFilterActive = true;
  memcpy(s_headCandidateMac, candidateMac, sizeof(s_headCandidateMac));
  s_headCandidateLastOpenMs = nowMs;
  return true;
}

bool pairingHeadIsOpen()
{
  return s_headOpen;
}

void pairingHeadTick(uint32_t nowMs)
{
  if (!s_headOpen) {
    return;
  }

  if ((int32_t)(nowMs - s_headOpenDeadlineMs) >= 0) {
    s_headOpen = false;
    s_headOpenDeadlineMs = 0;
    s_headRebindArmed = false;
    s_headCandidateFilterActive = false;
    memset(s_headCandidateMac, 0, sizeof(s_headCandidateMac));
  }
}

void pairingNodeEnterJoinMode(uint32_t nowMs)
{
  if (s_nodePaired) {
    return;
  }

  s_nodeInJoinMode = true;
  s_nodeJoinDeadlineMs = nowMs + PAIRING_NODE_JOIN_MS;
  s_joinSent = false;
  s_lastJoinMs = 0;
  s_offerNodeId = 0;
  s_offerReceived = false;
}

void pairingNodeExitJoinMode()
{
  s_nodeInJoinMode = false;
  s_nodeJoinDeadlineMs = 0;
  s_joinSent = false;
  s_offerNodeId = 0;
  s_offerReceived = false;
}

bool pairingNodeIsInJoinMode()
{
  return s_nodeInJoinMode;
}

bool pairingNodeJoinExpired(uint32_t nowMs)
{
  return s_nodeInJoinMode && ((int32_t)(nowMs - s_nodeJoinDeadlineMs) >= 0);
}

void pairingNodeTick(uint32_t nowMs)
{
  (void)nowMs;

  if (s_nodePaired) {
    pairingNodeExitJoinMode();
  }
}

void pairingNodeRestorePairedHead(const uint8_t headMac[6], uint16_t nodeId)
{
  if (!headMac || nodeId == 0) {
    return;
  }

  s_nodePaired = true;
  s_nodeId = nodeId;
  memcpy(s_nodeHeadMac, headMac, sizeof(s_nodeHeadMac));
  pairingNodeExitJoinMode();
  s_offerNodeId = 0;
  s_offerReceived = false;
  s_joinSent = false;
}

void pairingNodeSetUnpaired()
{
  s_nodePaired = false;
  s_nodeId = 0;
  memset(s_nodeHeadMac, 0, sizeof(s_nodeHeadMac));
  pairingNodeExitJoinMode();
  s_offerNodeId = 0;
  s_offerReceived = false;
  s_joinSent = false;
}

void pairingHeadFactoryReset()
{
  s_headPaired = false;
  s_headPairedNodeId = 0;
  memset(s_headPairedNodeMac, 0, sizeof(s_headPairedNodeMac));
  memset(s_headPairedNodeUid, 0, sizeof(s_headPairedNodeUid));
  headClearPairedRegistry();
  s_headPairSuccessEvent = false;
  s_pendingNodeId = 0;
  s_pendingSessionId = 0;
  memset(s_pendingNodeUid, 0, sizeof(s_pendingNodeUid));
  memset(s_pendingNodeMac, 0, sizeof(s_pendingNodeMac));
  s_nextNodeId = 1;
  s_headSessionId = esp_random();
  pairingHeadSetOpen(false);
  s_headRebindArmed = false;
  s_headCandidateLastOpenMs = 0;
  s_headNvsDirty = false;
  s_headNvsSaveDueMs = 0;
  s_headNvsRetryCount = 0;
  if (!pairingNvsClearHead()) {
    Serial.println("PAIRING(HEAD): NVS clear failed");
  }
  Serial.println("PAIRING(HEAD): factory reset complete");
}

void pairingNodeFactoryReset()
{
  pairingNodeSetUnpaired();
  s_nodeSessionId = 0;
  s_seenHead = false;
  s_multiHeadConflict = false;
  memset(s_seenHeadMac, 0, sizeof(s_seenHeadMac));
  s_beaconRxCount = 0;
  Serial.println("PAIRING(NODE): factory reset complete");
}

static void readFactoryUid(uint8_t out_uid[6])
{
  esp_read_mac(out_uid, ESP_MAC_WIFI_STA);
}

static bool macEq(const uint8_t a[6], const uint8_t b[6])
{
  return memcmp(a, b, 6) == 0;
}

static void macCopy(uint8_t dst[6], const uint8_t src[6])
{
  memcpy(dst, src, 6);
}

static void logMac(const char* prefix, const uint8_t mac[6])
{
  char out[18] = {0};
  macToString(mac, out, sizeof(out));
  Serial.print(prefix);
  Serial.println(out);
}

static void fillBase(PairBase& base, uint8_t messageType, uint32_t sessionId)
{
  base.protocolVersion = PAIRING_PROTOCOL_VERSION;
  base.messageType = messageType;
  base.sessionId = sessionId;
  memcpy(base.deviceUid, s_localFactoryUid, sizeof(base.deviceUid));
}

void pairingInitHead(uint8_t headId)
{
  s_isHead = true;
  s_isNode = false;
  s_headId = headId;

  readFactoryUid(s_localFactoryUid);
  s_headSessionId = esp_random();

  s_lastBeaconMs = 0;
  s_beaconTxCount = 0;

  s_nextNodeId = 1;

  s_pendingNodeId = 0;
  s_pendingSessionId = 0;
  memset(s_pendingNodeUid, 0, sizeof(s_pendingNodeUid));
  memset(s_pendingNodeMac, 0, sizeof(s_pendingNodeMac));

  s_headPaired = false;
  s_headPairedNodeId = 0;
  memset(s_headPairedNodeMac, 0, sizeof(s_headPairedNodeMac));
  memset(s_headPairedNodeUid, 0, sizeof(s_headPairedNodeUid));
  headClearPairedRegistry();
  s_headPairSuccessEvent = false;
  s_headOpen = false;
  s_headOpenDeadlineMs = 0;
  s_headRebindArmed = false;
  s_headCandidateFilterActive = false;
  memset(s_headCandidateMac, 0, sizeof(s_headCandidateMac));
  s_headCandidateLastOpenMs = 0;
  s_headNvsDirty = false;
  s_headNvsSaveDueMs = 0;
  s_headNvsRetryCount = 0;

  headRestorePairedRegistry();

  (void)espnowEnsurePeer(ESPNOW_BROADCAST_MAC, ESPNOW_CHANNEL, false);

  Serial.print("PAIRING(HEAD): init headId=");
  Serial.println((int)s_headId);
}

void pairingInitNode(uint8_t role)
{
  s_isNode = true;
  s_isHead = false;
  s_nodeRole = role;

  readFactoryUid(s_localFactoryUid);

  s_nodeSessionId = 0;

  s_nodePaired = false;
  s_nodeId = 0;
  memset(s_nodeHeadMac, 0, sizeof(s_nodeHeadMac));
  s_nodeInJoinMode = false;
  s_nodeJoinDeadlineMs = 0;

  s_seenHead = false;
  s_multiHeadConflict = false;
  memset(s_seenHeadMac, 0, sizeof(s_seenHeadMac));

  s_joinSent = false;
  s_lastJoinMs = 0;
  s_offerNodeId = 0;
  s_offerReceived = false;
  s_beaconRxCount = 0;

  (void)espnowEnsurePeer(ESPNOW_BROADCAST_MAC, ESPNOW_CHANNEL, false);

  Serial.print("PAIRING(NODE): init role=");
  Serial.println((int)s_nodeRole);
  logMac("PAIRING(NODE): uid=", s_localFactoryUid);
}

static void sendHeadBeacon()
{
  if (!s_headOpen) {
    return;
  }

  MsgBeacon beacon{};
  fillBase(beacon.base, MSG_BEACON, s_headSessionId);
  beacon.headId = s_headId;
  beacon.pairingOpen = s_headOpen ? 1 : 0;

  (void)espnowSend(ESPNOW_BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&beacon), sizeof(beacon));

  s_beaconTxCount++;
  if ((s_beaconTxCount % 5U) == 0U) {
    Serial.print("PAIRING(HEAD): beacon sent #");
    Serial.println((unsigned long)s_beaconTxCount);
  }
}

static void sendNodeJoinReq()
{
  MsgJoinReq join{};
  fillBase(join.base, MSG_JOIN_REQ, s_nodeSessionId);
  join.role = s_nodeRole;

  const bool ok = espnowSend(ESPNOW_BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&join), sizeof(join));
  s_joinSent = true;
  s_lastJoinMs = millis();
  s_offerReceived = false;

  if (ok) {
    Serial.println("PAIRING(NODE): JOIN_REQ sent");
  } else {
    Serial.println("PAIRING(NODE): JOIN_REQ send failed");
  }
}

void pairingTick()
{
  if (s_isHead) {
    const uint32_t now = millis();
    headPersistTick(now);
    if (now - s_lastBeaconMs >= 500U) {
      s_lastBeaconMs = now;
      sendHeadBeacon();
    }
  }

  if (s_isNode && !s_nodePaired && s_nodeInJoinMode && s_seenHead && !s_multiHeadConflict) {
    const uint32_t now = millis();
    if (!s_joinSent || (now - s_lastJoinMs >= 1000U)) {
      sendNodeJoinReq();
    }
  }
}

static void headHandleJoinReq(const uint8_t* src_mac, const MsgJoinReq* join)
{
  if (!src_mac || !join) {
    return;
  }

  if (!s_headOpen) {
    return;
  }

  if (s_headCandidateFilterActive && !macEq(src_mac, s_headCandidateMac)) {
    return;
  }

  if (s_headRebindArmed) {
    s_headRebindArmed = false;
  }

  uint16_t assignedNodeId = 0;
  int8_t existingSlot = headFindPairedSlotByMac(src_mac);
  if (existingSlot < 0) {
    existingSlot = headFindPairedSlotByUid(join->base.deviceUid);
  }

  if (existingSlot >= 0) {
    assignedNodeId = s_headPairedNodes[existingSlot].nodeId;
  } else {
    if (s_headPairedCount >= MAX_HEAD_PAIRED_NODES) {
      Serial.println("PAIRING(HEAD): paired registry full, JOIN_REQ ignored");
      return;
    }
    assignedNodeId = s_nextNodeId++;
  }

  s_pendingNodeId = assignedNodeId;
  s_pendingSessionId = join->base.sessionId;
  macCopy(s_pendingNodeUid, join->base.deviceUid);
  macCopy(s_pendingNodeMac, src_mac);

  MsgOffer offer{};
  fillBase(offer.base, MSG_OFFER, s_pendingSessionId);
  memcpy(offer.base.deviceUid, join->base.deviceUid, sizeof(offer.base.deviceUid));
  offer.nodeId = assignedNodeId;
  esp_read_mac(offer.headMac, ESP_MAC_WIFI_STA);
  offer.channel = ESPNOW_CHANNEL;

  (void)espnowEnsurePeer(src_mac, ESPNOW_CHANNEL, false);
  (void)espnowSend(src_mac, reinterpret_cast<const uint8_t*>(&offer), sizeof(offer));

  Serial.print("PAIRING(HEAD): OFFER sent nodeId=");
  Serial.println((unsigned long)assignedNodeId);
  logMac("PAIRING(HEAD): nodeMac=", src_mac);
}

static void headHandleConfirm(const uint8_t* src_mac, const MsgConfirm* confirm)
{
  if (!src_mac || !confirm) {
    return;
  }

  if (confirm->base.sessionId != s_pendingSessionId) {
    return;
  }

  if (confirm->nodeId != s_pendingNodeId) {
    return;
  }

  if (memcmp(confirm->base.deviceUid, s_pendingNodeUid, sizeof(s_pendingNodeUid)) != 0) {
    return;
  }

  s_headPaired = true;
  s_headPairedNodeId = confirm->nodeId;
  macCopy(s_headPairedNodeMac, src_mac);
  macCopy(s_headPairedNodeUid, confirm->base.deviceUid);
  const bool changed = headUpsertPairedNode(confirm->nodeId, src_mac, confirm->base.deviceUid);
  if (changed) {
    headSchedulePersist(millis());
    s_headPairSuccessEvent = true;
  }

  MsgAck ack{};
  fillBase(ack.base, MSG_ACK, confirm->base.sessionId);
  memcpy(ack.base.deviceUid, confirm->base.deviceUid, sizeof(ack.base.deviceUid));
  ack.nodeId = confirm->nodeId;
  ack.ok = 1;

  (void)espnowEnsurePeer(src_mac, ESPNOW_CHANNEL, false);
  (void)espnowSend(src_mac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));

  Serial.print("PAIRING(HEAD): Paired nodeId=");
  Serial.println((unsigned long)s_headPairedNodeId);
  logMac("PAIRING(HEAD): nodeMac=", s_headPairedNodeMac);
  logMac("PAIRING(HEAD): uid=", s_headPairedNodeUid);
}

static void nodeHandleBeacon(const uint8_t* src_mac, const MsgBeacon* beacon)
{
  if (!src_mac || !beacon) {
    return;
  }

  s_beaconRxCount++;
  if ((s_beaconRxCount % 5U) == 0U) {
    Serial.print("PAIRING(NODE): beacon seen #");
    Serial.println((unsigned long)s_beaconRxCount);
  }

  if (!s_seenHead) {
    s_seenHead = true;
    macCopy(s_seenHeadMac, src_mac);
    s_nodeSessionId = beacon->base.sessionId;
    logMac("PAIRING(NODE): headMac=", s_seenHeadMac);
    if (!s_nodePaired && s_nodeInJoinMode && !s_multiHeadConflict) {
      sendNodeJoinReq();
    }
    return;
  }

  if (!macEq(s_seenHeadMac, src_mac)) {
    s_multiHeadConflict = true;
    Serial.println("PAIRING(NODE): multiple heads detected, join blocked");
    return;
  }

  if (s_nodeSessionId != beacon->base.sessionId) {
    if (s_nodePaired && s_nodeSessionId == 0) {
      s_nodeSessionId = beacon->base.sessionId;
      return;
    }

    const bool wasPaired = s_nodePaired;
    s_nodeSessionId = beacon->base.sessionId;
    s_nodePaired = false;
    s_nodeId = 0;
    s_offerNodeId = 0;
    s_offerReceived = false;
    s_joinSent = false;
    if (!s_multiHeadConflict) {
      if (wasPaired) {
        Serial.println("PAIRING(NODE): head reboot/session changed, rejoin");
      } else {
        Serial.println("PAIRING(NODE): head session changed, rejoin");
      }
      if (s_nodeInJoinMode) {
        sendNodeJoinReq();
      }
    }
  }
}

static void nodeHandleOffer(const uint8_t* src_mac, const MsgOffer* offer)
{
  if (!src_mac || !offer || !s_seenHead || s_multiHeadConflict) {
    return;
  }

  if (!macEq(src_mac, s_seenHeadMac)) {
    return;
  }

  if (offer->base.sessionId != s_nodeSessionId) {
    return;
  }

  if (memcmp(offer->base.deviceUid, s_localFactoryUid, sizeof(offer->base.deviceUid)) != 0) {
    return;
  }

  if (offer->nodeId == 0) {
    return;
  }

  s_offerNodeId = offer->nodeId;
  s_offerReceived = true;
  macCopy(s_nodeHeadMac, src_mac);

  (void)espnowEnsurePeer(src_mac, ESPNOW_CHANNEL, false);

  MsgConfirm confirm{};
  fillBase(confirm.base, MSG_CONFIRM, s_nodeSessionId);
  confirm.nodeId = s_offerNodeId;

  (void)espnowSend(src_mac, reinterpret_cast<const uint8_t*>(&confirm), sizeof(confirm));

  Serial.print("PAIRING(NODE): OFFER received nodeId=");
  Serial.println((unsigned long)s_offerNodeId);
  Serial.println("PAIRING(NODE): CONFIRM sent");
}

static void nodeHandleAck(const uint8_t* src_mac, const MsgAck* ack)
{
  if (!src_mac || !ack || !s_seenHead) {
    return;
  }

  if (!macEq(src_mac, s_seenHeadMac)) {
    return;
  }

  if (ack->base.sessionId != s_nodeSessionId) {
    return;
  }

  if (!s_offerReceived) {
    return;
  }

  if (ack->ok != 1 || ack->nodeId == 0) {
    return;
  }

  if (ack->nodeId != s_offerNodeId) {
    return;
  }

  if (memcmp(ack->base.deviceUid, s_localFactoryUid, sizeof(ack->base.deviceUid)) != 0) {
    return;
  }

  s_nodePaired = true;
  s_nodeId = ack->nodeId;
  pairingNodeExitJoinMode();

  Serial.print("PAIRING(NODE): ACK received nodeId=");
  Serial.println((unsigned long)s_nodeId);
  logMac("PAIRING(NODE): headMac=", s_nodeHeadMac);
  logMac("PAIRING(NODE): uid=", s_localFactoryUid);
}

bool pairingOnRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (!src_mac || !data || len < (int)sizeof(PairBase)) {
    return false;
  }

  const PairBase* base = reinterpret_cast<const PairBase*>(data);
  if (base->protocolVersion != PAIRING_PROTOCOL_VERSION) {
    return false;
  }

  switch (base->messageType) {
    case MSG_BEACON:
      if (s_isNode && len == (int)sizeof(MsgBeacon)) {
        nodeHandleBeacon(src_mac, reinterpret_cast<const MsgBeacon*>(data));
        return true;
      }
      break;

    case MSG_JOIN_REQ:
      if (s_isHead && len == (int)sizeof(MsgJoinReq)) {
        headHandleJoinReq(src_mac, reinterpret_cast<const MsgJoinReq*>(data));
        return true;
      }
      break;

    case MSG_OFFER:
      if (s_isNode && len == (int)sizeof(MsgOffer)) {
        nodeHandleOffer(src_mac, reinterpret_cast<const MsgOffer*>(data));
        return true;
      }
      break;

    case MSG_CONFIRM:
      if (s_isHead && len == (int)sizeof(MsgConfirm)) {
        headHandleConfirm(src_mac, reinterpret_cast<const MsgConfirm*>(data));
        return true;
      }
      break;

    case MSG_ACK:
      if (s_isNode && len == (int)sizeof(MsgAck)) {
        nodeHandleAck(src_mac, reinterpret_cast<const MsgAck*>(data));
        return true;
      }
      break;

    default:
      break;
  }

  return false;
}

bool pairingHeadHasPairedNode()
{
  return s_headPairedCount > 0;
}

bool pairingHeadConsumePairSuccessEvent()
{
  const bool pending = s_headPairSuccessEvent;
  s_headPairSuccessEvent = false;
  return pending;
}

uint16_t pairingHeadPairedNodeId()
{
  return s_headPairedNodeId;
}

bool pairingHeadPairedNodeMac(uint8_t out_mac[6])
{
  if (!out_mac || !s_headPaired) {
    return false;
  }

  memcpy(out_mac, s_headPairedNodeMac, 6);
  return true;
}

bool pairingHeadIsKnownNode(uint16_t nodeId, const uint8_t mac[6])
{
  if (nodeId == 0 || !mac) {
    return false;
  }

  return headFindPairedSlotByNodeIdMac(nodeId, mac) >= 0;
}

bool pairingNodeIsPaired()
{
  return s_nodePaired;
}

uint16_t pairingNodeId()
{
  return s_nodeId;
}

bool pairingNodeHeadMac(uint8_t out_mac[6])
{
  if (!out_mac || !s_nodePaired) {
    return false;
  }

  memcpy(out_mac, s_nodeHeadMac, 6);
  return true;
}
