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


#define CONFIG_OFFSET_MAGB          0 // Up to 256ytes
#define CONFIG_OFFSET_WIFI_SSID     260 //28bytes (+4 to identify the config, "SSID" in ascii)
#define CONFIG_OFFSET_WIFI_PASS     292 //28bytes (+4 to identify the config, "PASS" in ascii)
#define CONFIG_OFFSET_WIFI_SIZE     28 //28bytes (+4 to identify the config, "SSID" in ascii)
char WiFiSSID[28] = "";
char WiFiPASS[28] = "";

bool speed_240_MHz = false;
bool firstDataSet = false;

uint8_t config_eeprom[FLASH_DATA_SIZE] = {};
bool haveAdapterConfig = false;
bool haveWifiConfig = false;

struct mobile_adapter adapter;
///////////////////////////////////////////////////////////////////////////////////////////////////
static uint8_t set_initial_data() {
    return 0xD2;
}

static uint8_t process_data(uint8_t data_in) {
    unsigned char data_out;
    data_out = mobile_transfer(&adapter, spi_get_hw(SPI_PORT)->dr);
    printf("IN: %02x OUT: %02x\n",data_in,data_out); 
    return data_out;
}

static inline void trigger_spi(spi_inst_t *spi, uint baudrate) {
    //spi_init
    reset_block(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);
    unreset_block_wait(spi == spi0 ? RESETS_RESET_SPI0_BITS : RESETS_RESET_SPI1_BITS);

    spi_set_baudrate(spi, baudrate);
    spi_set_format(spi, 8, SPI_CPOL_1, SPI_CPOL_1, SPI_MSB_FIRST);
    hw_set_bits(&spi_get_hw(spi)->dmacr, SPI_SSPDMACR_TXDMAE_BITS | SPI_SSPDMACR_RXDMAE_BITS);
    spi_set_slave(spi, true);
    
    // Set the first data into the buffer
    spi_get_hw(spi)->dr = 0xD2;
    firstDataSet = true;

    hw_set_bits(&spi_get_hw(spi)->cr1, SPI_SSPCR1_SSE_BITS);
}

unsigned long millis_latch = 0;
#define A_UNUSED __attribute__((unused))

void mobile_board_serial_disable(A_UNUSED void *user) {
    firstDataSet = false;
    spi_deinit(SPI_PORT);
}

void mobile_board_serial_enable(A_UNUSED void *user) {
    trigger_spi(SPI_PORT,SPI_BAUDRATE);
}

bool mobile_board_config_read(A_UNUSED void *user, void *dest, const uintptr_t offset, const size_t size) {
    ReadFlashConfig(config_eeprom);
    for(int i = 0; i < size; i++){
        ((char *)dest)[i] = (char)config_eeprom[i];
    }
    return true;
}

bool mobile_board_config_write(A_UNUSED void *user, const void *src, const uintptr_t offset, const size_t size) {
    for(int i = 0; i < size; i++){
        config_eeprom[i] = ((uint8_t *)src)[i];
    }
    SaveFlashConfig(config_eeprom);
    return true;
}

void mobile_board_time_latch(void *user, enum mobile_timers timer) {
    //millis_latch = millis();    

    //struct mobile_user *mobile = (struct mobile_user *)user;
    //mobile->bgb_clock_latch[timer] = mobile->bgb_clock;
    millis_latch = time_us_64();
}

bool mobile_board_time_check_ms(void *user, enum mobile_timers timer, unsigned ms) {
    //return (millis() - millis_latch) > (unsigned long)ms;
    
    //struct mobile_user *mobile = (struct mobile_user *)user;
    //return
        //((mobile->bgb_clock - mobile->bgb_clock_latch[timer]) & 0x7FFFFFFF) >=
        //(uint32_t)((double)ms * (1 << 21) / 1000);
    return (time_us_64() - millis_latch) > MS(ms);
}

void core1_context() {
    irq_set_mask_enabled(0xffffffff, false);

    //uint64_t time_us_now, last_readable = time_us_64();
    //firstDataSet = true; //Just make sure to set the data only one time

    while (true) {
        if (spi_is_readable(SPI_PORT)) {
            spi_get_hw(SPI_PORT)->dr = mobile_transfer(&adapter, spi_get_hw(SPI_PORT)->dr);
            //spi_get_hw(SPI_PORT)->dr = process_data(spi_get_hw(SPI_PORT)->dr);
        }
    }
}

void main(){
    speed_240_MHz = set_sys_clock_khz(240000, false);

    stdio_init_all();

    // Initialize SPI pins (only first time)
    gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI), gpio_pull_up(PIN_SPI_SCK);
    gpio_set_function(PIN_SPI_SIN, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_SOUT, GPIO_FUNC_SPI);  

    memset(WiFiSSID,0x00,sizeof(WiFiSSID));
    memset(WiFiPASS,0x00,sizeof(WiFiSSID));
    memset(config_eeprom,0x00,sizeof(config_eeprom));
    //SaveFlashConfig("test");
    

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

    if(haveWifiConfig){
        for(int i = CONFIG_OFFSET_WIFI_SSID; i < CONFIG_OFFSET_WIFI_SSID+CONFIG_OFFSET_WIFI_SIZE; i++){
            WiFiSSID[i-CONFIG_OFFSET_WIFI_SSID] = (char)config_eeprom[i];
        }
        for(int i = CONFIG_OFFSET_WIFI_PASS; i < CONFIG_OFFSET_WIFI_PASS+CONFIG_OFFSET_WIFI_SIZE; i++){
            WiFiPASS[i-CONFIG_OFFSET_WIFI_PASS] = (char)config_eeprom[i];
        }
    }

    multicore_launch_core1(core1_context);
    
    mobile_init(&adapter, NULL, NULL);

    while (true) {
        mobile_loop(&adapter);
    }
}