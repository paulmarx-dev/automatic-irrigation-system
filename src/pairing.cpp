#include "pairing.h"
#include "esp_now_helpers.h"
#include "common_config.h"

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

static bool s_headPaired = false;
static uint16_t s_headPairedNodeId = 0;
static uint8_t s_headPairedNodeMac[6] = {0};
static uint8_t s_headPairedNodeUid[6] = {0};

static bool s_nodePaired = false;
static uint16_t s_nodeId = 0;
static uint8_t s_nodeHeadMac[6] = {0};

static bool s_seenHead = false;
static bool s_multiHeadConflict = false;
static uint8_t s_seenHeadMac[6] = {0};

static bool s_joinSent = false;
static uint32_t s_lastJoinMs = 0;
static uint16_t s_offerNodeId = 0;

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

  s_seenHead = false;
  s_multiHeadConflict = false;
  memset(s_seenHeadMac, 0, sizeof(s_seenHeadMac));

  s_joinSent = false;
  s_lastJoinMs = 0;
  s_offerNodeId = 0;
  s_beaconRxCount = 0;

  (void)espnowEnsurePeer(ESPNOW_BROADCAST_MAC, ESPNOW_CHANNEL, false);

  Serial.print("PAIRING(NODE): init role=");
  Serial.println((int)s_nodeRole);
  logMac("PAIRING(NODE): uid=", s_localFactoryUid);
}

static void sendHeadBeacon()
{
  MsgBeacon beacon{};
  fillBase(beacon.base, MSG_BEACON, s_headSessionId);
  beacon.headId = s_headId;
  beacon.pairingOpen = 1;

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
    if (now - s_lastBeaconMs >= 500U) {
      s_lastBeaconMs = now;
      sendHeadBeacon();
    }
  }

  if (s_isNode && !s_nodePaired && s_seenHead && !s_multiHeadConflict) {
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

  const uint16_t assignedNodeId = s_nextNodeId++;

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
    if (!s_nodePaired && !s_multiHeadConflict) {
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
    s_nodeSessionId = beacon->base.sessionId;
    s_joinSent = false;
    if (!s_nodePaired && !s_multiHeadConflict) {
      Serial.println("PAIRING(NODE): head session changed, rejoin");
      sendNodeJoinReq();
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

  s_offerNodeId = offer->nodeId;
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

  if (ack->ok != 1 || ack->nodeId == 0) {
    return;
  }

  if (memcmp(ack->base.deviceUid, s_localFactoryUid, sizeof(ack->base.deviceUid)) != 0) {
    return;
  }

  s_nodePaired = true;
  s_nodeId = ack->nodeId;

  Serial.print("PAIRING(NODE): ACK received nodeId=");
  Serial.println((unsigned long)s_nodeId);
  logMac("PAIRING(NODE): headMac=", s_nodeHeadMac);
  logMac("PAIRING(NODE): uid=", s_localFactoryUid);
}

void pairingOnRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (!src_mac || !data || len < (int)sizeof(PairBase)) {
    return;
  }

  const PairBase* base = reinterpret_cast<const PairBase*>(data);
  if (base->protocolVersion != PAIRING_PROTOCOL_VERSION) {
    return;
  }

  switch (base->messageType) {
    case MSG_BEACON:
      if (s_isNode && len == (int)sizeof(MsgBeacon)) {
        nodeHandleBeacon(src_mac, reinterpret_cast<const MsgBeacon*>(data));
      }
      break;

    case MSG_JOIN_REQ:
      if (s_isHead && len == (int)sizeof(MsgJoinReq)) {
        headHandleJoinReq(src_mac, reinterpret_cast<const MsgJoinReq*>(data));
      }
      break;

    case MSG_OFFER:
      if (s_isNode && len == (int)sizeof(MsgOffer)) {
        nodeHandleOffer(src_mac, reinterpret_cast<const MsgOffer*>(data));
      }
      break;

    case MSG_CONFIRM:
      if (s_isHead && len == (int)sizeof(MsgConfirm)) {
        headHandleConfirm(src_mac, reinterpret_cast<const MsgConfirm*>(data));
      }
      break;

    case MSG_ACK:
      if (s_isNode && len == (int)sizeof(MsgAck)) {
        nodeHandleAck(src_mac, reinterpret_cast<const MsgAck*>(data));
      }
      break;

    default:
      break;
  }
}

bool pairingHeadHasPairedNode()
{
  return s_headPaired;
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
