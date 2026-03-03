#pragma once

#include <stdint.h>

bool cal_load(int32_t* outDryMv, int32_t* outWetMv);
bool cal_save(int32_t dryMv, int32_t wetMv);
bool cal_clear();
