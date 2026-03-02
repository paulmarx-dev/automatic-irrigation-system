#pragma once

#include <Arduino.h>
#include <stdint.h>

static constexpr uint8_t PAIRING_PROTOCOL_VERSION = 1;

enum PairMsgType : uint8_t {
  MSG_BEACON   = 1,
  MSG_JOIN_REQ = 2,
  MSG_OFFER    = 3,
  MSG_CONFIRM  = 4,
  MSG_ACK      = 5,
};

enum DeviceRole : uint8_t {
  ROLE_HEAD    = 1,
  ROLE_SENSOR  = 2,
  ROLE_CONTROL = 3,
};

#pragma pack(push, 1)

struct PairBase {
  uint8_t protocolVersion;
  uint8_t messageType;
  uint32_t sessionId;
  uint8_t deviceUid[6];
};

struct MsgBeacon {
  PairBase base;
  uint8_t headId;
  uint8_t pairingOpen;
};

struct MsgJoinReq {
  PairBase base;
  uint8_t role;
};

struct MsgOffer {
  PairBase base;
  uint16_t nodeId;
  uint8_t headMac[6];
  uint8_t channel;
};

struct MsgConfirm {
  PairBase base;
  uint16_t nodeId;
};

struct MsgAck {
  PairBase base;
  uint16_t nodeId;
  uint8_t ok;
};

#pragma pack(pop)

void pairingOnRecv(const uint8_t* src_mac, const uint8_t* data, int len);
void pairingInitHead(uint8_t headId);
void pairingInitNode(uint8_t role);
void pairingTick();

bool pairingHeadHasPairedNode();
uint16_t pairingHeadPairedNodeId();
bool pairingHeadPairedNodeMac(uint8_t out_mac[6]);

bool pairingNodeIsPaired();
uint16_t pairingNodeId();
bool pairingNodeHeadMac(uint8_t out_mac[6]);