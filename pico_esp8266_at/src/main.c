////////////////////////////////////
// - Need to fix the "eeprom" issue during the config request (maybe put the config at the end of the flash?)
// - Add the mobile_board_sock_* functions to handle the Request functions (only the necessary for now) - https://discord.com/channels/375413108467957761/541384270636384259/1017548420866654280
// -- Docs about AT commands 
// ---- https://www.espressif.com/sites/default/files/documentation/4a-esp8266_at_instruction_set_en.pdf
// ---- https://github.com/espressif/ESP8266_AT/wiki/ATE
// ---- https://docs.espressif.com/projects/esp-at/en/latest/esp32/AT_Command_Set/Basic_AT_Commands.htm
////////////////////////////////////

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
char WiFiSSID[32] = "";
char WiFiPASS[32] = "";

bool speed_240_MHz = false;
bool firstDataSet = false;

uint8_t config_eeprom[FLASH_DATA_SIZE] = {};
bool haveAdapterConfig = false;
bool haveWifiConfig = false;

struct mobile_adapter adapter;
//struct mobile_adapter_config adapter_config = MOBILE_ADAPTER_CONFIG_DEFAULT;
//struct mobile_user {
//    pthread_mutex_t mutex_serial;
//    pthread_mutex_t mutex_cond;
//    pthread_cond_t cond;
//    struct mobile_adapter adapter;
//    enum mobile_action action;
//    FILE *config;
//    _Atomic uint32_t bgb_clock;
//    _Atomic uint32_t bgb_clock_latch[MOBILE_MAX_TIMERS];
//    int sockets[MOBILE_MAX_CONNECTIONS];
//};
///////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////
//Self-made Debug Functions
///////////////////////////////////////

static uint8_t process_data(uint8_t data_in) {
    unsigned char data_out;
    data_out = mobile_transfer(&adapter, spi_get_hw(SPI_PORT)->dr);
    printf("IN: %02x OUT: %02x\n",data_in,data_out); 
    return data_out;
}

///////////////////////////////////////
// SPI Functions
///////////////////////////////////////

static uint8_t set_initial_data() {
    //Keeping this function if needs to do something during this call 
    return 0xD2;
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
    spi_get_hw(spi)->dr = set_initial_data();
    firstDataSet = true;

    hw_set_bits(&spi_get_hw(spi)->cr1, SPI_SSPCR1_SSE_BITS);
}

///////////////////////////////////////
// Mobile Adapter GB Functions
///////////////////////////////////////

unsigned long millis_latch = 0;
#define A_UNUSED __attribute__((unused))

void mobile_board_debug_log(void *user, const char *line){
    (void)user;
    fprintf(stderr, "%s\n", line);
}

void mobile_board_serial_disable(A_UNUSED void *user) {
    //struct mobile_user *mobile = (struct mobile_user *)user;
    //pthread_mutex_lock(&mobile->mutex_serial);
    firstDataSet = false;
    spi_deinit(SPI_PORT);
}

void mobile_board_serial_enable(A_UNUSED void *user) {
    
    
    //struct mobile_user *mobile = (struct mobile_user *)user;
    //pthread_mutex_unlock(&mobile->mutex_serial);
    trigger_spi(SPI_PORT,SPI_BAUDRATE);
}

bool mobile_board_config_read(A_UNUSED void *user, void *dest, const uintptr_t offset, const size_t size) {
    //for (size_t i = 0; i < size; i++) ((char *)dest)[i] = EEPROM.read(offset + i);
    
    //struct mobile_user *mobile = (struct mobile_user *)user;
    //fseek(mobile->config, offset, SEEK_SET);
    //return fread(dest, 1, size, mobile->config) == size;    
    ReadFlashConfig(config_eeprom);
    for(int i = 0; i < size; i++){
        ((char *)dest)[i] = (char)config_eeprom[offset + i];
    }
    return true;
}

bool mobile_board_config_write(A_UNUSED void *user, const void *src, const uintptr_t offset, const size_t size) {
    //for (size_t i = 0; i < size; i++) EEPROM.write(offset + i, ((char *)src)[i]);
    
    //struct mobile_user *mobile = (struct mobile_user *)user;
    //fseek(mobile->config, offset, SEEK_SET);
    //return fwrite(src, 1, size, mobile->config) == size;
    for(int i = 0; i < size; i++){
        config_eeprom[offset + i] = ((uint8_t *)src)[i];
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

bool mobile_board_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){}
void mobile_board_sock_close(void *user, unsigned conn){}
int mobile_board_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr){}
bool mobile_board_sock_listen(void *user, unsigned conn){}
bool mobile_board_sock_accept(void *user, unsigned conn){}
int mobile_board_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr){}
int mobile_board_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr){}

///////////////////////////////////////
// Mais Functions and Core 1 Loop
///////////////////////////////////////

void core1_context() {
    irq_set_mask_enabled(0xffffffff, false);
    //trigger_spi(SPI_PORT,SPI_BAUDRATE);
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

    // Initialize SPI pins
    gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI), gpio_pull_up(PIN_SPI_SCK);
    gpio_set_function(PIN_SPI_SIN, GPIO_FUNC_SPI), gpio_pull_up(PIN_SPI_SIN);
    gpio_set_function(PIN_SPI_SOUT, GPIO_FUNC_SPI), gpio_pull_down(PIN_SPI_SOUT);

    memset(WiFiSSID,0x00,sizeof(WiFiSSID));
    memset(WiFiPASS,0x00,sizeof(WiFiSSID));
    memset(config_eeprom,0x00,sizeof(config_eeprom));
    SaveFlashConfig("test");
    //FormatFlashConfig();

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
    }else{
        //Set a Default value (need to create a method to configure this setttings later. Maybe a dual boot with a custom GB rom?)
        sprintf(WiFiSSID,"SSID%s","WiFi_Network");
        sprintf(WiFiPASS,"PASS%s","P@$$w0rd");
        for(int i = CONFIG_OFFSET_WIFI_SSID-4; i < CONFIG_OFFSET_WIFI_SSID+CONFIG_OFFSET_WIFI_SIZE; i++){
            config_eeprom[i] = WiFiSSID[i-CONFIG_OFFSET_WIFI_SSID];
        }
        for(int i = CONFIG_OFFSET_WIFI_PASS-4; i < CONFIG_OFFSET_WIFI_PASS+CONFIG_OFFSET_WIFI_SIZE; i++){
            config_eeprom[i] = WiFiPASS[i-CONFIG_OFFSET_WIFI_PASS];
        }
    }

    //////////////////////////////////
    //Need to AT the AT functions to connect to the WiFi
    //////////////////////////////////

    //mobile_init(&mobile->adapter, mobile, &adapter_config);
    mobile_init(&adapter, NULL, NULL);
    multicore_launch_core1(core1_context);

    while (true) {
        mobile_loop(&adapter);
    }
}