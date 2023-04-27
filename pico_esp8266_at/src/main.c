////////////////////////////////////
// - Change the Strings comps to string finds and read the buffer directly?
// - Optimize the ESP_AT.H file! Specially to handle the UDP connections better (fast)
// - Request functions discussion - https://discord.com/channels/375413108467957761/541384270636384259/1017548420866654280
// - MOBILE_ENABLE_NO32BIT - try to build with this option enabled to handle GBA games
// -- Docs about AT commands 
// ---- https://www.espressif.com/sites/default/files/documentation/4a-esp8266_at_instruction_set_en.pdf
// ---- https://github.com/espressif/ESP8266_AT/wiki/ATE
// ---- https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/index.html
////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>

#include "hardware/irq.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"


#include "gblink.h"
#include "esp_at.h"
#include "flash_eeprom.h"
#include "libmobile_func.h"
#include "config_menu.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
bool speed_240_MHz = false;

//#define DEBUG_SIGNAL_PINS
//#define MOBILE_ENABLE_NO32BIT

///////////////////////////////////////////////////////////////////////////////////////////////////
//LED Config
#define LED_PIN       		  	25
#define LED_SET(A)    		  	(gpio_put(LED_PIN, (A)))
#define LED_ON        		  	LED_SET(true)
#define LED_OFF       		  	LED_SET(false)
#define LED_TOGGLE    		  	(gpio_put(LED_PIN, !gpio_get(LED_PIN)))

volatile uint64_t time_us_now = 0;
uint64_t last_readable = 0;

//Wi-Fi Controllers
bool isConnectedWiFi = false;
char WiFiSSID[28] = "WiFi_Network";
char WiFiPASS[28] = "P@$$w0rd";

//Control Bools
bool isESPDetected = false;
bool haveConfigToWrite = false;

volatile bool spiLock = false;

/////////////////////////////////
// MOBILE ADAPTER GB FUNCTIONS //
/////////////////////////////////
struct mobile_user *mobile;

static void impl_debug_log(void *user, const char *line){
    (void)user;
    fprintf(stderr, "%s\n", line);
}

static void impl_serial_disable(void *user) {
    #ifdef DEBUG_SIGNAL_PINS
        gpio_put(10, true);
    #endif
    struct mobile_user *mobile = (struct mobile_user *)user;
    while(spiLock);
    spi_deinit(SPI_PORT);    
}

static void impl_serial_enable(void *user, bool mode_32bit) {
    (void)mode_32bit;
    struct mobile_user *mobile = (struct mobile_user *)user;
    trigger_spi(SPI_PORT,SPI_BAUDRATE_256);
    #ifdef DEBUG_SIGNAL_PINS
        gpio_put(10, false);
    #endif
}

static bool impl_config_read(void *user, void *dest, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for(int i = 0; i < size; i++){
        ((char *)dest)[i] = (char)mobile->config_eeprom[OFFSET_MAGB + offset + i];
    }
    return true;
}

static bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for(int i = 0; i < size; i++){
        mobile->config_eeprom[OFFSET_MAGB + offset + i] = ((uint8_t *)src)[i];
    }
    haveConfigToWrite = true;
    last_readable = time_us_64();
    return true;
}

static void impl_time_latch(void *user, unsigned timer) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->esp_clock_latch[timer] = time_us_64();
}

static bool impl_time_check_ms(void *user, unsigned timer, unsigned ms) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    return ((time_us_64() - mobile->esp_clock_latch[timer]) >= MS(ms));
}

//Callbacks
static bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_open\n");
    return socket_impl_open(&mobile->esp_sockets[conn], conn, socktype, addrtype, bindport);
}

static void impl_sock_close(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_close\n");
    return socket_impl_close(&mobile->esp_sockets[conn]);
}

static int impl_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_connect\n"); 
    return socket_impl_connect(&mobile->esp_sockets[conn], addr);
}

static int impl_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_send\n");
    return socket_impl_send(&mobile->esp_sockets[conn], data, size, addr);
}

static int impl_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;    
    // printf("mobile_impl_sock_recv\n");
    return socket_impl_recv(&mobile->esp_sockets[conn], data, size, addr);
}

static bool impl_sock_listen(void *user, unsigned conn){ 
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_listen\n");
    return socket_impl_listen(&mobile->esp_sockets[conn]);
}

static bool impl_sock_accept(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_accept\n"); 
    return socket_impl_accept(&mobile->esp_sockets[conn]);
}

static void impl_update_number(void *user, enum mobile_number type, const char *number){
    struct mobile_user *mobile = (struct mobile_user *)user;
    char *dest = NULL;

    switch (type) {
        case MOBILE_NUMBER_USER: dest = mobile->number_user; break;
        case MOBILE_NUMBER_PEER: dest = mobile->number_peer; break;
        default: assert(false); return;
    }

    if (number) {
        strncpy(dest, number, MOBILE_MAX_NUMBER_SIZE);
        dest[MOBILE_MAX_NUMBER_SIZE] = '\0';
    } else {
        dest[0] = '\0';
    }
}



///////////////////////////////////////
// Main Functions and Core 1 Loop
///////////////////////////////////////

void core1_context() {
    irq_set_mask_enabled(0xffffffff, false);

    while (true) {
        if(spi_is_readable(SPI_PORT)){
            spiLock = true;
            spi_get_hw(SPI_PORT)->dr = mobile_transfer(mobile->adapter, spi_get_hw(SPI_PORT)->dr);
            spiLock = false;
        }
    }
}

void main(){
    //Setup Pico
    speed_240_MHz = set_sys_clock_khz(240000, false);
    stdio_init_all();

    #ifdef DEBUG_SIGNAL_PINS
    gpio_init(9);
    gpio_set_dir(9, GPIO_OUT);
    gpio_put(9, false);

    gpio_init(10);
    gpio_set_dir(10, GPIO_OUT);
    gpio_put(10, false);
    #endif

    // For toggle_led
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    LED_ON;

    // Initialize SPI pins
    gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI), gpio_pull_up(PIN_SPI_SCK);
    gpio_set_function(PIN_SPI_SIN, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_SOUT, GPIO_FUNC_SPI);

    //Libmobile Variables
    enum mobile_adapter_device device = MOBILE_ADAPTER_BLUE;
    bool device_unmetered = false;
    struct mobile_addr dns1 = {0};
    struct mobile_addr dns2 = {0};
    unsigned dns_port = MOBILE_DNS_PORT;
    unsigned p2p_port = MOBILE_DEFAULT_P2P_PORT;
    struct mobile_addr relay = {0};
    bool relay_token_update = false;
    unsigned char *relay_token = NULL;
    unsigned char relay_token_buf[MOBILE_RELAY_TOKEN_SIZE];

    mobile = malloc(sizeof(struct mobile_user));

    memset(mobile->config_eeprom,0x00,sizeof(mobile->config_eeprom));
    ReadFlashConfig(mobile->config_eeprom,WiFiSSID,WiFiPASS); 

    // Initialize mobile library
    mobile->adapter = mobile_new(mobile);
    mobile_def_debug_log(mobile->adapter, impl_debug_log);
    mobile_def_serial_disable(mobile->adapter, impl_serial_disable);
    mobile_def_serial_enable(mobile->adapter, impl_serial_enable);
    mobile_def_config_read(mobile->adapter, impl_config_read);
    mobile_def_config_write(mobile->adapter, impl_config_write);
    mobile_def_time_latch(mobile->adapter, impl_time_latch);
    mobile_def_time_check_ms(mobile->adapter, impl_time_check_ms);
    mobile_def_sock_open(mobile->adapter, impl_sock_open);
    mobile_def_sock_close(mobile->adapter, impl_sock_close);
    mobile_def_sock_connect(mobile->adapter, impl_sock_connect);
    mobile_def_sock_listen(mobile->adapter, impl_sock_listen);
    mobile_def_sock_accept(mobile->adapter, impl_sock_accept);
    mobile_def_sock_send(mobile->adapter, impl_sock_send);
    mobile_def_sock_recv(mobile->adapter, impl_sock_recv);
    mobile_def_update_number(mobile->adapter, impl_update_number);

    mobile_config_load(mobile->adapter);
    
    BootMenuConfig(mobile,WiFiSSID,WiFiPASS);

    //////////////////////
    // CONFIGURE THE ESP
    //////////////////////
    isESPDetected = EspAT_Init(UART_ID, BAUD_RATE, UART_TX_PIN, UART_RX_PIN);
    if(isESPDetected){
        isConnectedWiFi = ESP_ConnectWifi(UART_ID, WiFiSSID, WiFiPASS,SEC(10));
    }
    //////////////////
    // END CONFIGURE
    //////////////////
    
    if(isConnectedWiFi){
        mobile->action = MOBILE_ACTION_NONE;
        mobile->number_user[0] = '\0';
        mobile->number_peer[0] = '\0';
        for (int i = 0; i < MOBILE_MAX_TIMERS; i++) mobile->esp_clock_latch[i] = 0;
        for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
            mobile->esp_sockets[i].host_id = -1;
            mobile->esp_sockets[i].host_iptype = MOBILE_ADDRTYPE_NONE;
            mobile->esp_sockets[i].host_type = 0;
            mobile->esp_sockets[i].local_port = -1;
            mobile->esp_sockets[i].sock_status = false;
            mobile->esp_sockets[i].isServerOpened = false;
            mobile->esp_sockets[i].idpVal = 0;
            ESP_CloseSockConn(UART_ID,i);
        }

        multicore_launch_core1(core1_context);
        FlushATBuff();

        mobile_start(mobile->adapter);

        LED_OFF;
        while (true) {
            mobile_loop(mobile->adapter);

            if(haveConfigToWrite){
                time_us_now = time_us_64();
                if (time_us_now - last_readable > SEC(5)){
                    if(!spi_is_readable(SPI_PORT)){
                        multicore_reset_core1();
                        FormatFlashConfig();
                        SaveFlashConfig(mobile->config_eeprom);
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
    }else{
        printf("Error during Pico setup! =( \n");
        while(true){
            //do something
        }
    }
}
