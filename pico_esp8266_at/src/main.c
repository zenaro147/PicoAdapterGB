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
bool speed_240_MHz = false;

#define MKS(A)                  (A)
#define MS(A)                   ((A) * 1000)
#define SEC(A)                  ((A) * 1000 * 1000)

// SPI pins
#define SPI_PORT        spi0
#define SPI_BAUDRATE    64 * 1024 * 8
#define PIN_SPI_SIN     16
#define PIN_SPI_SCK     18
#define PIN_SPI_SOUT    19 

//UART pins
#define UART_ID uart1
#define BAUD_RATE 115200
#define UART_TX_PIN 4
#define UART_RX_PIN 5

volatile uint64_t time_us_now = 0;
uint64_t last_readable = 0;

#define MAGB_HOST "192.168.0.126"
#define MAGB_PORT 80

#define CONFIG_OFFSET_MAGB          0 // Up to 256ytes
#define CONFIG_OFFSET_WIFI_SSID     260 //28bytes (+4 to identify the config, "SSID" in ascii)
#define CONFIG_OFFSET_WIFI_PASS     292 //28bytes (+4 to identify the config, "PASS" in ascii)
#define CONFIG_OFFSET_WIFI_SIZE     28 //28bytes (+4 to identify the config, "SSID" in ascii)
char WiFiSSID[32] = "";
char WiFiPASS[32] = "";
bool isESPDetected = false;

uint8_t config_eeprom[FLASH_DATA_SIZE] = {};
bool haveAdapterConfig = false;
bool haveWifiConfig = false;
bool haveConfigToWrite = false;

struct mobile_adapter adapter;
struct mobile_adapter_config adapter_config = MOBILE_ADAPTER_CONFIG_DEFAULT;
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

    hw_set_bits(&spi_get_hw(spi)->cr1, SPI_SSPCR1_SSE_BITS);
}

///////////////////////////////////////
// Mobile Adapter GB Functions
///////////////////////////////////////
/*
you don't need to support everything it says right out of the gate, for example IPv6 isn't used currently.
and UDP is only necessary for DNS
similarly the bind() functionality is only used for receiving calls.
so you can just check for all the things you don't support and return errors for now 
This API might not map exactly to the AT commands the ESP has but it should be close-ish
For example, sock_open() should do nothing in your case but initialize a structure that keeps track of the socket's type, and then when sock_connect() is called, AT+CIPSTART needs to be called with the socket type specified in sock_open() and the address specified in sock_connect().
Same for sock_listen() but the AT command would be AT+CIPSERVER
sock_accept() would check if anything has connected to the AT+CIPSERVER socket
All of sock_connect(), sock_listen() and sock_accept() are for TCP
For UDP the destination address is specified in sock_send(), and apparently this mirrors the AT+CIPSENDEX command.
Similarly for TCP connections sock_send() is an AT+CIPSEND command.
sock_recv() would depend on whether you're using active or passive mode for receiving, but I'm seeing there's no passive mode reception command for UDP which might be an issue.
sock_recv() is also a funky one because it needs to signal to libmobile whether the connection has been closed (if it's a TCP connection) and it needs to know where the message came from (especially for UDP sockets) 

*/
unsigned long millis_latch = 0;
#define A_UNUSED __attribute__((unused))

void mobile_board_debug_log(void *user, const char *line){
    (void)user;
    fprintf(stderr, "%s\n", line);
}

void mobile_board_serial_disable(A_UNUSED void *user) {
    spi_deinit(SPI_PORT);
}

void mobile_board_serial_enable(A_UNUSED void *user) {  
    trigger_spi(SPI_PORT,SPI_BAUDRATE);
}

bool mobile_board_config_read(A_UNUSED void *user, void *dest, const uintptr_t offset, const size_t size) {
    for(int i = 0; i < size; i++){
        ((char *)dest)[i] = (char)config_eeprom[offset + i];
    }
    return true;
}

bool mobile_board_config_write(A_UNUSED void *user, const void *src, const uintptr_t offset, const size_t size) {
    for(int i = 0; i < size; i++){
        config_eeprom[offset + i] = ((uint8_t *)src)[i];
    }
    haveConfigToWrite = true;
    last_readable = time_us_64();
    return true;
}

void mobile_board_time_latch(void *user, enum mobile_timers timer) {
    millis_latch = time_us_64();
}

bool mobile_board_time_check_ms(void *user, enum mobile_timers timer, unsigned ms) {
    return (time_us_64() - millis_latch) > MS(ms);
}


bool mobile_board_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){
    //Mobile Adapter connection
    // user = 0x0
    // conn = 0
    // socktype = MOBILE_SOCKTYPE_UDP
    // addrtype = MOBILE_ADDRTYPE_IPV4
    // bindport = 0
    return false;
}
void mobile_board_sock_close(void *user, unsigned conn){
    //conn = 1
    sleep_ms(1);
}
int mobile_board_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr){
    // conn = 1
    // addr = 0x2 
    //      Type: (unknown: 0x4) 
    //      _addr4 - type:(unknown: 0x4) / port:3473408 / host:0 0 1 0)
    //      _addr6 - type:(unknown: 0x4) / port:3473408 / host:---)
    return -1;
}
bool mobile_board_sock_listen(void *user, unsigned conn){
    //conn = 1
    return false;
}
bool mobile_board_sock_accept(void *user, unsigned conn){
    //conn = 1
    return false;
}
int mobile_board_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr){
    //user = 0x200001d5 <flash_enable_xip_via_boot2+8> ????
    //conn = 1
    //data = 0x2
    //size = 268437649
    // addr = 0x200010bc <adapter> 
    //      Type: MOBILE_ADDRTYPE_NONE
    //      _addr4 - type:MOBILE_ADDRTYPE_NONE / port:1 / host:21 0 0 0)
    //      _addr6 - type:MOBILE_ADDRTYPE_NONE / port:1 / host:---)
    return -1;
}
int mobile_board_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr){
    //user = 0x200001d5 <flash_enable_xip_via_boot2+8>
    //conn = 1
    //data = 0x2
    //size = 268437649
    // addr = 0x200010bc <adapter> 
    //      Type: MOBILE_ADDRTYPE_NONE
    //      _addr4 - type:MOBILE_ADDRTYPE_NONE / port:1 / host:21 0 0 0)
    //      _addr6 - type:MOBILE_ADDRTYPE_NONE / port:1 / host:---)
    return -1;
}

///////////////////////////////////////
// Main Functions and Core 1 Loop
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

//////////////////////
// CONFIGURE THE ESP
//////////////////////
    isESPDetected = EspAT_Init(UART_ID, BAUD_RATE, UART_TX_PIN, UART_RX_PIN);
    if(isESPDetected){
        //test connection - Check 2 times in case the ESP return some echo 
        FlushATBuff(); // Reset RX Buffer
        SendESPcmd(UART_ID,"AT");
        char * resp = ReadESPcmd(SEC(1));
        if(strcmp(resp, "OK") == 0){
            printf("ESP-01 Connectivity: OK\n");
        }else{        
            printf("ESP-01 Connectivity: ERROR || Retrying...\n");
            resp = ReadESPcmd(SEC(1));
            if(strcmp(resp, "OK") == 0){
                printf("ESP-01 Connectivity: OK\n");
            }else{        
                printf("ESP-01 Connectivity: ERROR\n");
            }
        }

        // Disable echo - Check 2 times in case the ESP return some echo 
        SendESPcmd(UART_ID,"ATE0");
        resp = ReadESPcmd(SEC(2));
        if(strcmp(resp, "OK") == 0){
            printf("ESP-01 Disable Echo: OK\n");
        }else{        
            printf("ESP-01 Disable Echo: ERROR || Retrying...\n");
            resp = ReadESPcmd(SEC(2));
            if(strcmp(resp, "OK") == 0){
                printf("ESP-01 Disable Echo: OK\n");
            }else{        
                printf("ESP-01 Disable Echo: ERROR\n");
            }
        }

        // Set to Passive Mode to receive TCP info
        // AT+CIPRECVDATA=<size> | read the X amount of data from esp buffer
        // AT+CIPRECVLEN? | return the remaining  buffer size like this +CIPRECVLEN:636,0,0,0,0)
        // Also can read the actual size reading the +IPD value from "AT+CIPSEND" output: \r\n\r\nRecv 60 bytes\r\n\r\nSEND OK\r\n\r\n+IPD,636\r\nCLOSED\r\n
        SendESPcmd(UART_ID, "AT+CIPRECVMODE=1");
        resp = ReadESPcmd(SEC(2));
        if(strcmp(resp, "OK") == 0){
            printf("ESP-01 Passive Mode: OK\n");
        }else{        
            printf("ESP-01 Passive Mode: ERROR\n");
        }

        isConnectedWiFi = ConnectESPWiFi(UART_ID, WiFiSSID, WiFiPASS,SEC(10));
        if(!isConnectedWiFi){
            printf("ESP-01 Connecting Wifi: ERROR\n");
        }
    }
//////////////////
// END CONFIGURE
//////////////////
    if(isConnectedWiFi){
        //mobile_init(&mobile->adapter, mobile, &adapter_config);
        //mobile_init(&adapter, NULL, NULL);
        mobile_init(&adapter, NULL, &adapter_config);

        multicore_launch_core1(core1_context);
        while (true) {
            mobile_loop(&adapter);

            if(haveConfigToWrite){
                time_us_now = time_us_64();
                if (time_us_now - last_readable > SEC(5)){
                    if(!spi_is_readable(SPI_PORT)){
                        multicore_reset_core1();
                        SaveFlashConfig(config_eeprom);
                        haveConfigToWrite = false;
                        time_us_now = 0;
                        last_readable = 0;                    
                        multicore_launch_core1(core1_context);
                    }else{
                        last_readable = time_us_now;
                    }
                }
            }
        }

        //bool reqStatus = SendESPGetReq(UART_ID, MAGB_HOST, MAGB_PORT, "/01/CGB-B9AJ/index.php");
        //bool reqStatus = SendESPGetReq(UART_ID, MAGB_HOST, MAGB_PORT, "/cgb/download?name=/01/CGB-BXTJ/tamago/tamago0a.pkm"); 
        //bool reqStatus = SendESPGetReq(UART_ID, MAGB_HOST, MAGB_PORT, "/01/CGB-BXTJ/tamago/tamago0a.pkm");        
        //ReadESPGetReq(UART_ID,700); //The value will be stored into buffGETReq array
    }
}
