#pragma once

#include <stdint.h>

class WebServer;

void headProvisioningInit(WebServer* server);
void headProvisioningTick(uint32_t nowMs);

void headProvisioningOpenSession(uint32_t nowMs);
void headProvisioningCloseSession();
bool headProvisioningIsSetupStarted();

bool headProvisioningHandleTriplePressReset(uint32_t nowMs, bool pairingWindowOpen);
