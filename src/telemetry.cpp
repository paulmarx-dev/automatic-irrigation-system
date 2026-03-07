#include "telemetry.h"

#include <Arduino.h>
#include <string.h>
#include <esp_system.h>

#include "common_config.h"
#include "esp_now_helpers.h"
#include "pairing.h"
#include "protocol.h"

#if defined(DEVICE_ROLE_SENSOR)
#include "sensors.h"
#include "button.h"
#include "leds.h"
#include "pairing_nvs.h"
#include "sensor_remote_control.h"

static const unsigned long ACK_TIMEOUT_MS = 200;
static const uint8_t MAX_RETRIES = 3;
static const uint8_t MAX_NO_ACK_CYCLES_BEFORE_REJOIN = 3;

static uint16_t s_telemetrySeq = 0;
static uint16_t s_lastSentSeq = 0;
static bool s_waitingAck = false;
static uint32_t s_ackDeadlineMs = 0;
static uint8_t s_retryCount = 0;
static uint32_t s_lastSendStartMs = 0;
static uint8_t s_noAckCycles = 0;
static uint32_t s_nextTelemetryDueMs = 0;

static MsgTelemetry s_pendingTelemetry{};
static uint8_t s_pendingHeadMac[6] = {0};

static bool sendPendingTelemetry()
{
  (void)espnowEnsurePeer(s_pendingHeadMac, ESPNOW_CHANNEL, false);
  return espnowSend(s_pendingHeadMac, reinterpret_cast<const uint8_t*>(&s_pendingTelemetry), sizeof(s_pendingTelemetry));
}

static uint32_t randomBoundedMs(uint32_t maxExclusive)
{
  if (maxExclusive == 0) {
    return 0;
  }
  return static_cast<uint32_t>(esp_random() % maxExclusive);
}

static uint32_t deterministicNodePhaseOffsetMs()
{
  const uint16_t nodeId = pairingNodeId();
  if (nodeId == 0 || TELEMETRY_SCHEDULE_MAX_NODES == 0) {
    return 0;
  }

  uint32_t slotWidthMs = TELEMETRY_PHASE_SPREAD_MS / TELEMETRY_SCHEDULE_MAX_NODES;
  if (slotWidthMs == 0) {
    slotWidthMs = 1;
  }

  const uint32_t slot = (static_cast<uint32_t>(nodeId - 1) % TELEMETRY_SCHEDULE_MAX_NODES);
  return slot * slotWidthMs;
}

static uint32_t nextTelemetryIntervalMs()
{
  return TELEMETRY_BASE_INTERVAL_MS + randomBoundedMs(TELEMETRY_INTERVAL_JITTER_MS + 1);
}

static uint32_t retryBackoffMs(uint8_t retryIndex)
{
  const uint32_t jitter = randomBoundedMs(120);
  if (retryIndex == 0) {
    return 200 + jitter;
  }
  if (retryIndex == 1) {
    return 400 + jitter;
  }
  return 800 + jitter;
}

void telemetryInit()
{
  s_telemetrySeq = 0;
  s_lastSentSeq = 0;
  s_waitingAck = false;
  s_ackDeadlineMs = 0;
  s_retryCount = 0;
  s_lastSendStartMs = 0;
  s_noAckCycles = 0;
  s_nextTelemetryDueMs = 0;
  memset(&s_pendingTelemetry, 0, sizeof(s_pendingTelemetry));
  memset(s_pendingHeadMac, 0, sizeof(s_pendingHeadMac));
}

void telemetryOnRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (!src_mac || !data || len < (int)sizeof(MsgHdr)) {
    return;
  }

  const MsgHdr* hdr = reinterpret_cast<const MsgHdr*>(data);
  if (hdr->ver != PROTO_VER) {
    return;
  }

  if (hdr->type == MSG_REMOTE_BUTTON) {
    if (len != (int)sizeof(MsgRemoteButton)) {
      return;
    }
    if (!pairingNodeIsPaired()) {
      return;
    }
    if (hdr->nodeId != pairingNodeId()) {
      return;
    }

    const MsgRemoteButton* cmd = reinterpret_cast<const MsgRemoteButton*>(data);
    const bool accepted = sensorHandleRemoteButtonAction(cmd->action, millis());

    Serial.print("REMOTE_BTN action=");
    Serial.print((unsigned long)cmd->action);
    Serial.print(" accepted=");
    Serial.println(accepted ? 1 : 0);
    return;
  }

  if (hdr->type != MSG_TELEMETRY_ACK || len != (int)sizeof(MsgTelemetryAck)) {
    return;
  }

  const MsgTelemetryAck* ack = reinterpret_cast<const MsgTelemetryAck*>(data);

  if (!s_waitingAck) {
    return;
  }

  if (memcmp(src_mac, s_pendingHeadMac, 6) != 0) {
    return;
  }

  if (ack->hdr.nodeId != pairingNodeId()) {
    return;
  }

  if (ack->ackSeq != s_lastSentSeq) {
    return;
  }

  if (ack->status == TELEMETRY_ACK_STATUS_NOT_PAIRED) {
    Serial.println("PAIRING(NODE): head reports NOT_PAIRED, clearing local pairing");
    pairingInitNode(ROLE_SENSOR);
    if (!pairingNvsClearNode()) {
      Serial.println("PAIRING(NODE): NVS clear failed");
    }
    Serial.println("PAIRING(NODE): waiting for manual pairing action");
    ledsTriggerOnce(LED_MODE_ERROR_ONCE);
    s_waitingAck = false;
    s_noAckCycles = 0;
    return;
  }

  if (ack->status != TELEMETRY_ACK_STATUS_OK) {
    Serial.print("TELEMETRY_ACK unexpected status=");
    Serial.println((unsigned long)ack->status);
    s_waitingAck = false;
    return;
  }

  const uint32_t rttMs = millis() - s_lastSendStartMs;
  s_waitingAck = false;
  s_noAckCycles = 0;

  if (buttonIsDebugEnabled()) {
    ledsPulseOnce(30);
  }

  Serial.print("TELEMETRY_ACK ok=1 seq=");
  Serial.print((unsigned long)ack->ackSeq);
  Serial.print(" nodeId=");
  Serial.print((unsigned long)ack->hdr.nodeId);
  Serial.print(" rttMs=");
  Serial.println((unsigned long)rttMs);
}

void telemetryTickSensor(const SensorMeasurement* measurement, bool hasMeasurement, uint32_t nowMs)
{
  if (!pairingNodeIsPaired()) {
    s_waitingAck = false;
    s_noAckCycles = 0;
    s_nextTelemetryDueMs = 0;
    return;
  }

  if (s_waitingAck) {
    if ((int32_t)(nowMs - s_ackDeadlineMs) >= 0) {
      if (s_retryCount < MAX_RETRIES) {
        const uint32_t backoffMs = retryBackoffMs(s_retryCount);
        const bool sent = sendPendingTelemetry();
        s_retryCount++;
        s_ackDeadlineMs = nowMs + backoffMs;

        Serial.print("TELEMETRY retry sent=");
        Serial.print(sent ? 1 : 0);
        Serial.print(" seq=");
        Serial.print((unsigned long)s_lastSentSeq);
        Serial.print(" retry=");
        Serial.print((unsigned long)s_retryCount);
        Serial.print(" waitMs=");
        Serial.println((unsigned long)backoffMs);
      } else {
        Serial.print("TELEMETRY failed no-ack seq=");
        Serial.print((unsigned long)s_lastSentSeq);
        Serial.print(" retries=");
        Serial.println((unsigned long)s_retryCount);
        if (buttonIsDebugEnabled()) {
          ledsPulseOnce(120);
        }
        s_waitingAck = false;
        if (s_noAckCycles < 255) {
          s_noAckCycles++;
        }
        if (s_noAckCycles >= MAX_NO_ACK_CYCLES_BEFORE_REJOIN) {
          Serial.println("PAIRING(NODE): no-ack threshold reached, keep paired and continue telemetry retries");
          s_noAckCycles = MAX_NO_ACK_CYCLES_BEFORE_REJOIN;
        }
      }
    }
    return;
  }

  if (!hasMeasurement || !measurement) {
    return;
  }

  if (s_nextTelemetryDueMs == 0) {
    s_nextTelemetryDueMs = nowMs +
                           TELEMETRY_FIRST_SEND_MIN_DELAY_MS +
                           deterministicNodePhaseOffsetMs() +
                           randomBoundedMs(TELEMETRY_FIRST_SEND_JITTER_MS + 1);
  }

  if ((int32_t)(nowMs - s_nextTelemetryDueMs) < 0) {
    return;
  }
  s_nextTelemetryDueMs = nowMs + nextTelemetryIntervalMs();

  uint8_t headMac[6] = {0};
  if (!pairingNodeHeadMac(headMac)) {
    return;
  }

  s_pendingTelemetry.hdr.ver = PROTO_VER;
  s_pendingTelemetry.hdr.type = MSG_TELEMETRY;
  s_pendingTelemetry.hdr.seq = ++s_telemetrySeq;
  s_pendingTelemetry.hdr.nodeId = pairingNodeId();

  s_pendingTelemetry.moisturePermille = measurement->moisturePermille;
  s_pendingTelemetry.moistureRawMv = measurement->moistureRawMv;
  s_pendingTelemetry.batteryRawMv = measurement->batteryRawMv;
  s_pendingTelemetry.batteryEstMv = measurement->batteryEstMv;
  s_pendingTelemetry.flags = FLAG_DIAG_RAW_PRESENT | FLAG_BATT_EST_VALID;
  s_pendingTelemetry.reserved = 0;

  memcpy(s_pendingHeadMac, headMac, 6);

  const bool sent = sendPendingTelemetry();
  if (buttonIsDebugEnabled()) {
    ledsPulseOnce(20);
  }
  s_lastSentSeq = s_pendingTelemetry.hdr.seq;
  s_waitingAck = true;
  s_retryCount = 0;
  s_ackDeadlineMs = nowMs + ACK_TIMEOUT_MS;
  s_lastSendStartMs = nowMs;

  if (!sent) {
    s_waitingAck = false;
  }

  Serial.print("TELEMETRY sent=");
  Serial.print(sent ? 1 : 0);
  Serial.print(" seq=");
  Serial.print((unsigned long)s_pendingTelemetry.hdr.seq);
  Serial.print(" nodeId=");
  Serial.print((unsigned long)s_pendingTelemetry.hdr.nodeId);
  Serial.print(" moisturePermille=");
  Serial.print((unsigned long)s_pendingTelemetry.moisturePermille);
  Serial.print(" moistureRawMv=");
  Serial.print((unsigned long)s_pendingTelemetry.moistureRawMv);
  Serial.print(" batteryRawMv=");
  Serial.print((unsigned long)s_pendingTelemetry.batteryRawMv);
  Serial.print(" batteryEstMv=");
  Serial.print((unsigned long)s_pendingTelemetry.batteryEstMv);
  Serial.print(" flags=");
  Serial.println((unsigned long)s_pendingTelemetry.flags);
}

#elif defined(DEVICE_ROLE_HEAD)

#include "button.h"
#include "leds.h"

static const uint8_t MAX_NODE_REGISTRY = 8;
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint32_t EXPECTED_TELEMETRY_PERIOD_MS = TELEMETRY_BASE_INTERVAL_MS + TELEMETRY_INTERVAL_JITTER_MS;
static const uint32_t NODE_SUSPECT_TIMEOUT_MS = 3 * EXPECTED_TELEMETRY_PERIOD_MS;
static const uint32_t NODE_OFFLINE_TIMEOUT_MS = 8 * EXPECTED_TELEMETRY_PERIOD_MS;

static const char* nodeStateToText(TelemetryHeadNodeState state)
{
  switch (state) {
    case TELEMETRY_HEAD_NODE_ONLINE:
      return "online";
    case TELEMETRY_HEAD_NODE_SUSPECT:
      return "suspect";
    case TELEMETRY_HEAD_NODE_OFFLINE:
      return "offline";
    default:
      return "unknown";
  }
}

struct NodeTelemetryState {
  bool used;
  TelemetryHeadNodeState state;
  TelemetryHeadBatteryState batteryState;
  bool hasLastSeq;
  uint16_t nodeId;
  uint16_t moisturePermille;
  uint16_t batteryEstMv;
  uint8_t mac[6];
  uint32_t lastSeenMs;
  uint16_t lastSeq;
  uint32_t rxPackets;
  uint32_t rxDuplicates;
  uint32_t rxInvalid;
  uint32_t ackOkSent;
  uint32_t ackNotPairedSent;
};

static NodeTelemetryState s_nodes[MAX_NODE_REGISTRY] = {};
static uint16_t s_ackSeq = 0;

static TelemetryHeadBatteryState classifyBatteryState(uint16_t batteryEstMv)
{
  if (batteryEstMv <= BATTERY_NEEDS_REPLACEMENT_MV) {
    return TELEMETRY_HEAD_BATTERY_NEEDS_REPLACEMENT;
  }
  if (batteryEstMv <= BATTERY_CRITICAL_MV) {
    return TELEMETRY_HEAD_BATTERY_CRITICAL;
  }
  return TELEMETRY_HEAD_BATTERY_OK;
}

void telemetryInit()
{
  memset(s_nodes, 0, sizeof(s_nodes));
  s_ackSeq = 0;
}

static NodeTelemetryState* getOrCreateNodeState(uint16_t nodeId, const uint8_t src_mac[6], uint32_t nowMs)
{
  NodeTelemetryState* emptySlot = nullptr;
  NodeTelemetryState* oldestSlot = &s_nodes[0];

  for (uint8_t i = 0; i < MAX_NODE_REGISTRY; ++i) {
    NodeTelemetryState* entry = &s_nodes[i];
    if (entry->used) {
      if (entry->nodeId == nodeId) {
        memcpy(entry->mac, src_mac, 6);
        return entry;
      }
      if (entry->lastSeenMs < oldestSlot->lastSeenMs) {
        oldestSlot = entry;
      }
    } else if (!emptySlot) {
      emptySlot = entry;
    }
  }

  NodeTelemetryState* target = emptySlot ? emptySlot : oldestSlot;
  target->used = true;
  target->state = TELEMETRY_HEAD_NODE_ONLINE;
  target->batteryState = TELEMETRY_HEAD_BATTERY_OK;
  target->hasLastSeq = false;
  target->nodeId = nodeId;
  target->moisturePermille = 0;
  target->batteryEstMv = 0;
  memcpy(target->mac, src_mac, 6);
  target->lastSeenMs = nowMs;
  target->lastSeq = 0;
  target->rxPackets = 0;
  target->rxDuplicates = 0;
  target->rxInvalid = 0;
  target->ackOkSent = 0;
  target->ackNotPairedSent = 0;
  return target;
}

static NodeTelemetryState* findNodeState(uint16_t nodeId, const uint8_t src_mac[6])
{
  for (uint8_t i = 0; i < MAX_NODE_REGISTRY; ++i) {
    NodeTelemetryState* entry = &s_nodes[i];
    if (!entry->used) {
      continue;
    }
    if (entry->nodeId == nodeId && memcmp(entry->mac, src_mac, 6) == 0) {
      return entry;
    }
  }
  return nullptr;
}

static void logTelemetry(const MsgTelemetry* telemetry, const uint8_t src_mac[6])
{
  char macBuf[18] = {0};
  macToString(src_mac, macBuf, sizeof(macBuf));

  Serial.print("[nodeId=");
  Serial.print((unsigned long)telemetry->hdr.nodeId);
  Serial.print(" mac=");
  Serial.print(macBuf);
  Serial.print("] telemetry seq=");
  Serial.print((unsigned long)telemetry->hdr.seq);
  Serial.print(" moisturePermille=");
  Serial.print((unsigned long)telemetry->moisturePermille);
  Serial.print(" moistureRawMv=");
  Serial.print((unsigned long)telemetry->moistureRawMv);
  Serial.print(" batteryRawMv=");
  Serial.print((unsigned long)telemetry->batteryRawMv);
  Serial.print(" batteryEstMv=");
  Serial.print((unsigned long)telemetry->batteryEstMv);
  const bool rawPresent = (telemetry->flags & FLAG_DIAG_RAW_PRESENT) != 0;
  const bool calValid = (telemetry->flags & FLAG_CAL_VALID) != 0;
  const bool battEstValid = (telemetry->flags & FLAG_BATT_EST_VALID) != 0;

  Serial.print(" flags=0x");
  if (telemetry->flags < 0x10) {
    Serial.print('0');
  }
  Serial.print((unsigned long)telemetry->flags, HEX);
  Serial.print(" rawPresent=");
  Serial.print(rawPresent ? 1 : 0);
  Serial.print(" battEstValid=");
  Serial.print(battEstValid ? 1 : 0);
  Serial.print(" calValid=");
  Serial.println(calValid ? 1 : 0);
}

static void sendTelemetryAck(const uint8_t src_mac[6], uint16_t nodeId, uint16_t telemetrySeq, uint8_t status)
{
  MsgTelemetryAck ack{};
  ack.hdr.ver = PROTO_VER;
  ack.hdr.type = MSG_TELEMETRY_ACK;
  ack.hdr.seq = ++s_ackSeq;
  ack.hdr.nodeId = nodeId;
  ack.ackSeq = telemetrySeq;
  ack.status = status;
  ack.reserved = 0;

  (void)espnowEnsurePeer(src_mac, ESPNOW_CHANNEL, false);
  const bool sent = espnowSend(src_mac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));

  NodeTelemetryState* nodeState = findNodeState(nodeId, src_mac);
  if (nodeState) {
    if (status == TELEMETRY_ACK_STATUS_OK) {
      nodeState->ackOkSent++;
    } else if (status == TELEMETRY_ACK_STATUS_NOT_PAIRED) {
      nodeState->ackNotPairedSent++;
    }
  }

  char macBuf[18] = {0};
  macToString(src_mac, macBuf, sizeof(macBuf));
  Serial.print("[nodeId=");
  Serial.print((unsigned long)nodeId);
  Serial.print(" mac=");
  Serial.print(macBuf);
  Serial.print("] telemetry_ack sent=");
  Serial.print(sent ? 1 : 0);
  Serial.print(" status=");
  Serial.print((unsigned long)status);
  Serial.print(" ackSeq=");
  Serial.println((unsigned long)telemetrySeq);
}

void telemetryOnRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (!src_mac || !data) {
    return;
  }

  if (len != (int)sizeof(MsgTelemetry)) {
    return;
  }

  const MsgTelemetry* telemetry = reinterpret_cast<const MsgTelemetry*>(data);
  if (telemetry->hdr.ver != PROTO_VER || telemetry->hdr.type != MSG_TELEMETRY) {
    return;
  }

  const uint32_t nowMs = millis();
  const bool fromCurrentPair = pairingHeadIsKnownNode(telemetry->hdr.nodeId, src_mac);
  NodeTelemetryState* nodeState = nullptr;
  if (fromCurrentPair) {
    nodeState = getOrCreateNodeState(telemetry->hdr.nodeId, src_mac, nowMs);
    if (!nodeState) {
      return;
    }
  } else {
    nodeState = findNodeState(telemetry->hdr.nodeId, src_mac);
  }

  if (!fromCurrentPair) {
    if (nodeState) {
      nodeState->rxInvalid++;
    }
    sendTelemetryAck(src_mac, telemetry->hdr.nodeId, telemetry->hdr.seq, TELEMETRY_ACK_STATUS_NOT_PAIRED);
    return;
  }

  if (nodeState->state != TELEMETRY_HEAD_NODE_ONLINE) {
    nodeState->state = TELEMETRY_HEAD_NODE_ONLINE;
    char macBuf[18] = {0};
    macToString(src_mac, macBuf, sizeof(macBuf));
    Serial.print("[nodeId=");
    Serial.print((unsigned long)nodeState->nodeId);
    Serial.print(" mac=");
    Serial.print(macBuf);
    Serial.println("] online");
  }

  nodeState->lastSeenMs = nowMs;

  const bool isDuplicate = nodeState->hasLastSeq && (nodeState->lastSeq == telemetry->hdr.seq);
  if (isDuplicate) {
    nodeState->rxDuplicates++;
  } else {
    nodeState->hasLastSeq = true;
    nodeState->lastSeq = telemetry->hdr.seq;
    nodeState->moisturePermille = telemetry->moisturePermille;
    nodeState->batteryEstMv = telemetry->batteryEstMv;
    nodeState->batteryState = classifyBatteryState(telemetry->batteryEstMv);
    nodeState->rxPackets++;
    logTelemetry(telemetry, src_mac);
  }

  if (buttonIsDebugEnabled()) {
    ledsPulseOnce(120);
  }

  sendTelemetryAck(src_mac, telemetry->hdr.nodeId, telemetry->hdr.seq, TELEMETRY_ACK_STATUS_OK);
}

void telemetryTickHead(uint32_t nowMs)
{
  for (uint8_t i = 0; i < MAX_NODE_REGISTRY; ++i) {
    NodeTelemetryState* entry = &s_nodes[i];
    if (!entry->used) {
      continue;
    }

    const uint32_t sinceLastMs = static_cast<uint32_t>(nowMs - entry->lastSeenMs);

    TelemetryHeadNodeState nextState = TELEMETRY_HEAD_NODE_ONLINE;
    if (sinceLastMs >= NODE_OFFLINE_TIMEOUT_MS) {
      nextState = TELEMETRY_HEAD_NODE_OFFLINE;
    } else if (sinceLastMs >= NODE_SUSPECT_TIMEOUT_MS) {
      nextState = TELEMETRY_HEAD_NODE_SUSPECT;
    }

    if (nextState != entry->state) {
      entry->state = nextState;
      char macBuf[18] = {0};
      macToString(entry->mac, macBuf, sizeof(macBuf));
      Serial.print("[nodeId=");
      Serial.print((unsigned long)entry->nodeId);
      Serial.print(" mac=");
      Serial.print(macBuf);
      Serial.print("] ");
      Serial.println(nodeStateToText(entry->state));
    }
  }
}

uint8_t telemetryHeadGetPresence(TelemetryHeadNodePresence* outNodes, uint8_t maxNodes)
{
  if (!outNodes || maxNodes == 0) {
    return 0;
  }

  uint8_t written = 0;
  for (uint8_t i = 0; i < MAX_NODE_REGISTRY && written < maxNodes; ++i) {
    const NodeTelemetryState* entry = &s_nodes[i];
    if (!entry->used) {
      continue;
    }

    outNodes[written].used = true;
    outNodes[written].state = entry->state;
    outNodes[written].batteryState = entry->batteryState;
    outNodes[written].nodeId = entry->nodeId;
    outNodes[written].moisturePermille = entry->moisturePermille;
    outNodes[written].batteryEstMv = entry->batteryEstMv;
    memcpy(outNodes[written].mac, entry->mac, 6);
    outNodes[written].lastSeenMs = entry->lastSeenMs;
    outNodes[written].rxPackets = entry->rxPackets;
    outNodes[written].rxDuplicates = entry->rxDuplicates;
    outNodes[written].rxInvalid = entry->rxInvalid;
    outNodes[written].ackOkSent = entry->ackOkSent;
    outNodes[written].ackNotPairedSent = entry->ackNotPairedSent;
    written++;
  }
  return written;
}

void telemetryHeadClearPresence()
{
  memset(s_nodes, 0, sizeof(s_nodes));
}

bool telemetryHeadRemovePresenceByNodeId(uint16_t nodeId)
{
  if (nodeId == 0) {
    return false;
  }

  for (uint8_t i = 0; i < MAX_NODE_REGISTRY; ++i) {
    NodeTelemetryState* entry = &s_nodes[i];
    if (!entry->used) {
      continue;
    }
    if (entry->nodeId != nodeId) {
      continue;
    }
    memset(entry, 0, sizeof(*entry));
    return true;
  }
  return false;
}

bool telemetryHeadSendRemoteButtonAction(uint16_t nodeId, uint8_t action)
{
  const bool isCalibrationAction =
      action == REMOTE_BUTTON_CALIBRATE_START || action == REMOTE_BUTTON_CALIBRATE_MEASURE_WET;
  const bool isIrrigationAction =
      action == REMOTE_BUTTON_IRRIGATION_START || action == REMOTE_BUTTON_IRRIGATION_STOP;

  if (!isCalibrationAction && !isIrrigationAction) {
    return false;
  }

  if (nodeId == 0) {
    if (!isIrrigationAction) {
      return false;
    }

    MsgRemoteButton command{};
    command.hdr.ver = PROTO_VER;
    command.hdr.type = MSG_REMOTE_BUTTON;
    command.hdr.seq = ++s_ackSeq;
    command.hdr.nodeId = 0;
    command.action = action;
    command.reserved = 0;

    const bool sent = espnowSend(BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&command), sizeof(command));
    Serial.print("[nodeId=*] remote_btn broadcast sent=");
    Serial.print(sent ? 1 : 0);
    Serial.print(" action=");
    Serial.println((unsigned long)action);
    return sent;
  }

  NodeTelemetryState* node = nullptr;
  for (uint8_t i = 0; i < MAX_NODE_REGISTRY; ++i) {
    NodeTelemetryState* entry = &s_nodes[i];
    if (!entry->used || entry->nodeId != nodeId) {
      continue;
    }
    node = entry;
    break;
  }

  if (!node) {
    return false;
  }

  if (node->state != TELEMETRY_HEAD_NODE_ONLINE) {
    return false;
  }

  MsgRemoteButton command{};
  command.hdr.ver = PROTO_VER;
  command.hdr.type = MSG_REMOTE_BUTTON;
  command.hdr.seq = ++s_ackSeq;
  command.hdr.nodeId = nodeId;
  command.action = action;
  command.reserved = 0;

  (void)espnowEnsurePeer(node->mac, ESPNOW_CHANNEL, false);
  const bool sent = espnowSend(node->mac, reinterpret_cast<const uint8_t*>(&command), sizeof(command));

  char macBuf[18] = {0};
  macToString(node->mac, macBuf, sizeof(macBuf));
  Serial.print("[nodeId=");
  Serial.print((unsigned long)nodeId);
  Serial.print(" mac=");
  Serial.print(macBuf);
  Serial.print("] remote_btn sent=");
  Serial.print(sent ? 1 : 0);
  Serial.print(" action=");
  Serial.println((unsigned long)action);

  return sent;
}

void telemetryTickSensor(const SensorMeasurement* measurement, bool hasMeasurement, uint32_t nowMs)
{
  (void)measurement;
  (void)hasMeasurement;
  (void)nowMs;
}

#else

void telemetryInit() {}

void telemetryOnRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  (void)src_mac;
  (void)data;
  (void)len;
}

void telemetryTickSensor(const SensorMeasurement* measurement, bool hasMeasurement, uint32_t nowMs)
{
  (void)measurement;
  (void)hasMeasurement;
  (void)nowMs;
}

void telemetryTickHead(uint32_t nowMs)
{
  (void)nowMs;
}

uint8_t telemetryHeadGetPresence(TelemetryHeadNodePresence* outNodes, uint8_t maxNodes)
{
  (void)outNodes;
  (void)maxNodes;
  return 0;
}

void telemetryHeadClearPresence() {}

bool telemetryHeadRemovePresenceByNodeId(uint16_t nodeId)
{
  (void)nodeId;
  return false;
}

bool telemetryHeadSendRemoteButtonAction(uint16_t nodeId, uint8_t action)
{
  (void)nodeId;
  (void)action;
  return false;
}

#endif
