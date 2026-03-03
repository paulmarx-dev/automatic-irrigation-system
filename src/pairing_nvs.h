#pragma once

#include <stdint.h>

bool pairingNvsLoadNode(uint8_t outHeadMac[6]);
bool pairingNvsSaveNode(const uint8_t headMac[6]);
bool pairingNvsClearNode();
