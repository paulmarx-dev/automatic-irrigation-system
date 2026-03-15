#pragma once

#include <stddef.h>
#include <stdint.h>

class WebServer;

void headProvisioningInit(WebServer* server);
void headProvisioningTick(uint32_t nowMs);
void headProvisioningFactoryReset();
size_t headProvisioningComposeWebStatusJson(char* body, size_t bodySize, uint32_t nowMs);
size_t headProvisioningComposeUnitStatusJson(char* body, size_t bodySize, uint32_t nowMs);
