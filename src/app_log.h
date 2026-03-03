#pragma once

#include <Arduino.h>
#include <esp_mac.h>

#include "common_config.h"
#include "esp_now_helpers.h"

#ifndef LOG_LEVEL
#define LOG_LEVEL 2
#endif

#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_WARN  1
#define LOG_LEVEL_INFO  2

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOGI(fmt, ...) Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#else
#define LOGI(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOGW(fmt, ...) Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
#else
#define LOGW(fmt, ...)
#endif

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOGE(fmt, ...) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#else
#define LOGE(fmt, ...)
#endif

static inline void logStartupCommon(const char* role, bool pairedKnown, bool paired)
{
  uint8_t factoryMac[6] = {0};
  char factoryMacBuf[18] = {0};
  if (esp_read_mac(factoryMac, ESP_MAC_WIFI_STA) == ESP_OK) {
    macToString(factoryMac, factoryMacBuf, sizeof(factoryMacBuf));
  } else {
    strncpy(factoryMacBuf, "unknown", sizeof(factoryMacBuf) - 1);
  }

  LOGI("startup");
  LOGI("role=%s", role);
  LOGI("protocol=%u", PROTOCOL_VERSION);
  LOGI("deviceUID=%s", factoryMacBuf);
  LOGI("channel=%u", ESPNOW_CHANNEL);
  if (pairedKnown) {
    LOGI("paired=%s", paired ? "yes" : "no");
  } else {
    LOGI("paired=N/A");
  }
}