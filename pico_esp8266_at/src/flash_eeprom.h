#pragma once

#include "hardware/flash.h"

#define KEY_CONFIG "CONFIG"
#define KEY_SSID "SSID"
#define KEY_PASS "PASS"

#define FLASH_DATA_SIZE (FLASH_PAGE_SIZE * 3)
#define FLASH_TARGET_OFFSET (FLASH_DATA_SIZE * 1024)

#define OFFSET_CONFIG 0
#define OFFSET_MAGB 16
#define OFFSET_SSID MOBILE_CONFIG_SIZE+OFFSET_MAGB
#define OFFSET_PASS OFFSET_SSID+32

//Time Config
#define MKS(A)                  (A)
#define MS(A)                   ((A) * 1000)
#define SEC(A)                  ((A) * 1000 * 1000)

void FormatFlashConfig();
bool ReadConfigOption(uint8_t * buff, int offset, char *key, int datasize, char *varConfig);
bool ReadFlashConfig(uint8_t * buff, char * WiFiSSID, char * WiFiPASS);
void SaveFlashConfig(uint8_t * buff);
void RefreshConfigBuff(uint8_t * buff, char * WiFiSSID, char * WiFiPASS);