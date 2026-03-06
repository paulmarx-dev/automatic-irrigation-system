#pragma once

#include <stdint.h>

class WebServer;

void headProvisioningInit(WebServer* server);
void headProvisioningTick(uint32_t nowMs);
