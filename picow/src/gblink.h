#pragma once

#include "hardware/spi.h"
#include "hardware/resets.h"

// SPI pins
#define SPI_PORT            spi0
#define SPI_BAUDRATE_512    64 * 1024 * 8
#define SPI_BAUDRATE_256    32 * 1024 * 8
#define PIN_SPI_SIN         16
#define PIN_SPI_SCK         18
#define PIN_SPI_SOUT        19 

static inline void trigger_spi(spi_inst_t *spi, uint baudrate);