#pragma once

#include "libmobile_func.h"
#include "flash_eeprom.h"

#include "hardware/spi.h"
#include "hardware/resets.h"

#include <mobile.h>

// SPI pins
#define SPI_PORT            spi0
#define SPI_BAUDRATE_512    64 * 1024 * 8
#define SPI_BAUDRATE_256    32 * 1024 * 8
#define PIN_SPI_SIN         16
#define PIN_SPI_SCK         18
#define PIN_SPI_SOUT        19 

struct mobile_user {
    struct mobile_adapter *adapter;
    enum mobile_action action;
	unsigned long esp_clock_latch[MOBILE_MAX_TIMERS];
    uint8_t config_eeprom[FLASH_DATA_SIZE];
    struct esp_sock_config esp_sockets[MOBILE_MAX_CONNECTIONS];
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
};

void trigger_spi(spi_inst_t *spi, uint baudrate);