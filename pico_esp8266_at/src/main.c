#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "hardware/resets.h"
//#include "hardware/clocks.h"
#include "hardware/flash.h"

#include "esp_at.h"
#include "flash_eeprom.h"

#include "libmobile/mobile.h"
///////////////////////////////////////////////////////////////////////////////////////////////////

#define MKS(A)                  (A)
#define MS(A)                   ((A) * 1000)
#define SEC(A)                  ((A) * 1000 * 1000)

// SPI mode
#define SPI_PORT        spi0
#define SPI_BAUDRATE    64 * 1024 * 8
#define PIN_SPI_SIN     16
#define PIN_SPI_SCK     18
#define PIN_SPI_SOUT    19 

bool speed_240_MHz = false;
bool firstDataSet = false;

//Test array data
uint8_t image_data[8] = {9,8,7,6,5,4,3,2};
int array_pointer = 0;

uint8_t config_eeprom[FLASH_DATA_SIZE] = {};
bool haveAdapterConfig = false;
bool haveWifiConfig = false;

struct mobile_adapter adapter;
///////////////////////////////////////////////////////////////////////////////////////////////////
static uint8_t set_initial_data() {
    return 0xD2;
}

static inline void trigger_spi(spi_inst_t *spi, uint baudrate, bool mode) {
    if(mode){
        // Initialize SPI pins (only first time)
        gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI), gpio_pull_up(PIN_SPI_SCK);
        gpio_set_function(PIN_SPI_SIN, GPIO_FUNC_SPI);
        gpio_set_function(PIN_SPI_SOUT, GPIO_FUNC_SPI);        
    }

    //spi_init
    reset_block(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);
    unreset_block_wait(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);

    spi_set_baudrate(spi, baudrate);
    spi_set_format(spi, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    hw_set_bits(&spi_get_hw(spi)->dmacr, SPI_SSPDMACR_TXDMAE_BITS | SPI_SSPDMACR_RXDMAE_BITS);
    spi_set_slave(spi, true);

    hw_set_bits(&spi_get_hw(spi)->cr1, SPI_SSPCR1_SSE_BITS);
    
    // Set the first data into the buffer
    //spi_get_hw(spi)->dr = set_initial_data();
    spi_get_hw(spi)->dr = 0xD2;
}


unsigned long millis_latch = 0;

#define A_UNUSED __attribute__((unused))

void mobile_board_serial_disable(A_UNUSED void *user) {
    spi_deinit(SPI_PORT);
}

void mobile_board_serial_enable(A_UNUSED void *user) {
    trigger_spi(SPI_PORT,SPI_BAUDRATE,false);
}

bool mobile_board_config_read(A_UNUSED void *user, void *dest, const uintptr_t offset, const size_t size) {
    //for (size_t i = 0; i < size; i++) ((char *)dest)[i] = EEPROM.read(offset + i);
    return true;
}

bool mobile_board_config_write(A_UNUSED void *user, const void *src, const uintptr_t offset, const size_t size) {
    //for (size_t i = 0; i < size; i++) EEPROM.write(offset + i, ((char *)src)[i]);
    return true;
}

void mobile_board_time_latch(A_UNUSED void *user) {
    //millis_latch = millis();
}

bool mobile_board_time_check_ms(A_UNUSED void *user, unsigned ms) {
    //return (millis() - millis_latch) > (unsigned long)ms;
}

void core1_context() {
    irq_set_mask_enabled(0xffffffff, false);

    //Enable SPI
    trigger_spi(SPI_PORT,SPI_BAUDRATE,true);

    uint64_t time_us_now, last_readable = time_us_64();
    firstDataSet = true; //Just make sure to set the data only one time

    while (true) {
        time_us_now = time_us_64();
        if (spi_is_readable(SPI_PORT)) {
            last_readable = time_us_now;
            if (firstDataSet){
                firstDataSet = false;
            }
            //spi_get_hw(SPI_PORT)->dr = process_data(spi_get_hw(SPI_PORT)->dr);
            spi_get_hw(SPI_PORT)->dr = mobile_transfer(&adapter, spi_get_hw(SPI_PORT)->dr);
        }
        if (time_us_now - last_readable > MS(200) && !firstDataSet) {
            trigger_spi(SPI_PORT,SPI_BAUDRATE,false);
            last_readable = time_us_now;
            firstDataSet = true;
        }
    }
}

void main(){
    speed_240_MHz = set_sys_clock_khz(240000, false);

    stdio_init_all();

    uint8_t setConfig = ReadFlashConfig(config_eeprom); 
    switch (setConfig){
        case 0:
            haveAdapterConfig = false;
            haveWifiConfig = false;
        break;
        case 1:
            haveAdapterConfig = true;
            haveWifiConfig = false;
        break;
        case 2:
            haveAdapterConfig = true;
            haveWifiConfig = true;
        break;
        case 3:
            haveAdapterConfig = false;
            haveWifiConfig = true;
        break;
        default:
        break;
    }

    multicore_launch_core1(core1_context);
    
    mobile_init(&adapter, NULL, NULL);

    while (true) {
        mobile_loop(&adapter);
    }
}