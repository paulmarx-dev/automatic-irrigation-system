#pragma once

#include <stdint.h>

bool pairingNvsLoadNode(uint8_t outHeadMac[6], uint16_t* outNodeId);
bool pairingNvsSaveNode(const uint8_t headMac[6], uint16_t nodeId);
bool pairingNvsClearNode();
