#pragma once

#include <stdint.h>

static constexpr uint8_t PAIRING_NVS_MAX_HEAD_NODES = 8;

struct PairingHeadNodeNvsRecord {
	uint16_t nodeId;
	uint8_t mac[6];
	uint8_t uid[6];
};

bool pairingNvsLoadNode(uint8_t outHeadMac[6], uint16_t* outNodeId);
bool pairingNvsSaveNode(const uint8_t headMac[6], uint16_t nodeId);
bool pairingNvsClearNode();

bool pairingNvsLoadHead(PairingHeadNodeNvsRecord outNodes[PAIRING_NVS_MAX_HEAD_NODES],
												uint8_t* outCount,
												uint16_t* outNextNodeId);
bool pairingNvsSaveHead(const PairingHeadNodeNvsRecord nodes[PAIRING_NVS_MAX_HEAD_NODES],
												uint8_t count,
												uint16_t nextNodeId);
bool pairingNvsClearHead();
