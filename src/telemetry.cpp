#include "telemetry.h"

#include <Arduino.h>
#include <string.h>

#include "common_config.h"
#include "esp_now_helpers.h"
#include "pairing.h"
#include "protocol.h"

#if defined(DEVICE_ROLE_SENSOR)
#include "sensors.h"
#include "button.h"
#include "leds.h"
#include "pairing_nvs.h"

static const unsigned long TELEMETRY_INTERVAL_MS = 5000;
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
static uint32_t s_lastTelemetryMs = 0;

static MsgTelemetry s_pendingTelemetry{};
static uint8_t s_pendingHeadMac[6] = {0};

static bool sendPendingTelemetry()
{
  (void)espnowEnsurePeer(s_pendingHeadMac, ESPNOW_CHANNEL, false);
  return espnowSend(s_pendingHeadMac, reinterpret_cast<const uint8_t*>(&s_pendingTelemetry), sizeof(s_pendingTelemetry));
}

static uint32_t retryBackoffMs(uint8_t retryIndex)
{
  if (retryIndex == 0) {
    return 200;
  }
  if (retryIndex == 1) {
    return 400;
  }
  return 800;
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
  s_lastTelemetryMs = 0;
  memset(&s_pendingTelemetry, 0, sizeof(s_pendingTelemetry));
  memset(s_pendingHeadMac, 0, sizeof(s_pendingHeadMac));
}

void telemetryOnRecv(const uint8_t* src_mac, const uint8_t* data, int len)
{
  if (!src_mac || !data || len != (int)sizeof(MsgTelemetryAck)) {
    return;
  }

  const MsgTelemetryAck* ack = reinterpret_cast<const MsgTelemetryAck*>(data);
  if (ack->hdr.ver != PROTO_VER || ack->hdr.type != MSG_TELEMETRY_ACK) {
    return;
  }

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
    pairingNodeEnterJoinMode(millis());
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
          Serial.println("PAIRING(NODE): no-ack threshold reached, force rejoin");
          pairingInitNode(ROLE_SENSOR);
          s_waitingAck = false;
          s_noAckCycles = 0;
          return;
        }
      }
    }
    return;
  }

  if (!hasMeasurement || !measurement) {
    return;
  }

  if (nowMs - s_lastTelemetryMs < TELEMETRY_INTERVAL_MS) {
    return;
  }
  s_lastTelemetryMs = nowMs;

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
static const uint32_t REBIND_OPEN_MS = 10000;
static const uint32_t REBIND_COOLDOWN_MS = 30000;

struct NodeTelemetryState {
  bool used;
  bool hasLastSeq;
  uint16_t nodeId;
  uint8_t mac[6];
  uint32_t lastSeenMs;
  uint16_t lastSeq;
};

static NodeTelemetryState s_nodes[MAX_NODE_REGISTRY] = {};
static uint16_t s_ackSeq = 0;

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
        entry->lastSeenMs = nowMs;
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
  target->hasLastSeq = false;
  target->nodeId = nodeId;
  memcpy(target->mac, src_mac, 6);
  target->lastSeenMs = nowMs;
  target->lastSeq = 0;
  return target;
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
  if (!src_mac || !data || len != (int)sizeof(MsgTelemetry)) {
    return;
  }

  const MsgTelemetry* telemetry = reinterpret_cast<const MsgTelemetry*>(data);
  if (telemetry->hdr.ver != PROTO_VER || telemetry->hdr.type != MSG_TELEMETRY) {
    return;
  }

  const uint16_t pairedNodeId = pairingHeadPairedNodeId();
  uint8_t pairedMac[6] = {0};
  const bool hasPairedMac = pairingHeadPairedNodeMac(pairedMac);
  const bool fromCurrentPair = pairedNodeId != 0 &&
                               hasPairedMac &&
                               memcmp(src_mac, pairedMac, 6) == 0 &&
                               telemetry->hdr.nodeId == pairedNodeId;

  if (!fromCurrentPair) {
    sendTelemetryAck(src_mac, telemetry->hdr.nodeId, telemetry->hdr.seq, TELEMETRY_ACK_STATUS_NOT_PAIRED);

    const bool opened = pairingHeadOpenCandidateWindow(src_mac, millis(), REBIND_OPEN_MS, REBIND_COOLDOWN_MS);
    if (opened) {
      char macBuf[18] = {0};
      macToString(src_mac, macBuf, sizeof(macBuf));
      Serial.print("PAIRING(HEAD): rebind window opened for candidate mac=");
      Serial.println(macBuf);
    }
    return;
  }

  const uint32_t nowMs = millis();
  NodeTelemetryState* nodeState = getOrCreateNodeState(telemetry->hdr.nodeId, src_mac, nowMs);
  if (!nodeState) {
    return;
  }

  const bool isDuplicate = nodeState->hasLastSeq && (nodeState->lastSeq == telemetry->hdr.seq);
  if (!isDuplicate) {
    nodeState->hasLastSeq = true;
    nodeState->lastSeq = telemetry->hdr.seq;
    logTelemetry(telemetry, src_mac);
  }

  if (buttonIsDebugEnabled()) {
    ledsPulseOnce(120);
  }

  sendTelemetryAck(src_mac, telemetry->hdr.nodeId, telemetry->hdr.seq, TELEMETRY_ACK_STATUS_OK);
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

#endif
