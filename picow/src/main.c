////////////////////////////////////
// -- MOBILE_ENABLE_NO32BIT - try to build with this option enabled to handle GBA games
////////////////////////////////////
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/cyw43_arch.h"

#include <mobile.h>
#include <mobile_inet.h>

#include "gblink.h"
#include "flash_eeprom.h"
#include "picow_socket.h"
#include "socket_impl.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
bool speed_240_MHz = false;

//#define DEBUG_SIGNAL_PINS

// #define ERASE_EEPROM //Encomment this to ERASE ALL stored config (including Adapter config)
// #define CONFIG_MODE //Uncomment this if you want to reconfigure something

///////////////////////////////////////////////////////////////////////////////////////////////////

//Wi-Fi Controllers
bool isConnectedWiFi = false;
char WiFiSSID[28] = "WiFi_Network";
char WiFiPASS[28] = "P@$$w0rd";

//Control Flash Write
bool haveConfigToWrite = false;

volatile bool spiLock = false;

//LED Config
#define LED_PIN       		  	CYW43_WL_GPIO_LED_PIN
#define LED_SET(A)    		  	(cyw43_arch_gpio_put(LED_PIN, (A)))
#define LED_ON        		  	LED_SET(true)
#define LED_OFF       		  	LED_SET(false)
#define LED_TOGGLE    		  	(cyw43_arch_gpio_put(LED_PIN, !cyw43_arch_gpio_get(LED_PIN)))

volatile uint64_t time_us_now_check = 0;
uint64_t last_readable_check = 0;

////////////////////////
// AUXILIAR FUNCTIONS //
////////////////////////
void main_parse_addr(struct mobile_addr *dest, char *argv){
    unsigned char ip[MOBILE_INET_PTON_MAXLEN];
    int rc = mobile_inet_pton(MOBILE_INET_PTON_ANY, argv, ip);

    struct mobile_addr4 *dest4 = (struct mobile_addr4 *)dest;
    struct mobile_addr6 *dest6 = (struct mobile_addr6 *)dest;
    switch (rc) {
        case MOBILE_INET_PTON_IPV4:
            dest4->type = MOBILE_ADDRTYPE_IPV4;
            memcpy(dest4->host, ip, sizeof(dest4->host));
            break;
        case MOBILE_INET_PTON_IPV6:
            dest6->type = MOBILE_ADDRTYPE_IPV6;
            memcpy(dest6->host, ip, sizeof(dest6->host));
            break;
        default:
            break;
    }
}

static bool main_parse_hex(unsigned char *buf, char *str, unsigned size){
    unsigned char x = 0;
    for (unsigned i = 0; i < size * 2; i++) {
        char c = str[i];
        if (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'A' && c <= 'F') c -= 'A' - 10;
        else if (c >= 'a' && c <= 'f') c -= 'a' - 10;
        else return false;

        x <<= 4;
        x |= c;

        if (i % 2 == 1) {
            buf[i / 2] = x;
            x = 0;
        }
    }
    return true;
}

void main_set_port(struct mobile_addr *dest, unsigned port){
    struct mobile_addr4 *dest4 = (struct mobile_addr4 *)dest;
    struct mobile_addr6 *dest6 = (struct mobile_addr6 *)dest;
    switch (dest->type) {
    case MOBILE_ADDRTYPE_IPV4:
        dest4->port = port;
        break;
    case MOBILE_ADDRTYPE_IPV6:
        dest6->port = port;
        break;
    default:
        break;
    }
}

/////////////////////////////////
// MOBILE ADAPTER GB FUNCTIONS //
/////////////////////////////////
unsigned long millis_latch = 0;
#define A_UNUSED __attribute__((unused))

struct mobile_user {
    struct mobile_adapter *adapter;
    enum mobile_action action;
    uint8_t config_eeprom[FLASH_DATA_SIZE];
    struct socket_impl socket[MOBILE_MAX_CONNECTIONS];
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
};
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
    last_readable_check = time_us_64();
    return true;
}

static void impl_time_latch(void *user, unsigned timer) {
    millis_latch = time_us_64();
}

static bool impl_time_check_ms(void *user, unsigned timer, unsigned ms) {
    return (time_us_64() - millis_latch) > MS(ms);
}

//Callbacks
static bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_open\n");
    return socket_impl_open(&mobile->socket[conn], socktype, addrtype, bindport);
}

static void impl_sock_close(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_close\n");
    return socket_impl_close(&mobile->socket[conn]);
}

static int impl_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_connect\n"); 
    return socket_impl_connect(&mobile->socket[conn], addr);
}

static int impl_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_send\n");
    return socket_impl_send(&mobile->socket[conn], data, size, addr);
}

static int impl_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_recv\n");
    return socket_impl_recv(&mobile->socket[conn], data, size, addr);
}



static bool impl_sock_listen(void *user, unsigned conn){ 
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_listen\n");
    return socket_impl_listen(&mobile->socket[conn]);
}

static bool impl_sock_accept(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    printf("mobile_impl_sock_accept\n"); 
    return socket_impl_accept(&mobile->socket[conn]);
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

///////////////////////////////
// PICO W AUXILIAR FUNCTIONS //
///////////////////////////////
bool PicoW_Connect_WiFi(char *ssid, char *psk, uint32_t timeout){
    cyw43_pm_value(CYW43_NO_POWERSAVE_MODE,200,1,1,10);
    cyw43_arch_enable_sta_mode();

    printf("Connecting to Wi-Fi...\n");
    //if (cyw43_arch_wifi_connect_timeout_ms(ssid, psk, CYW43_AUTH_WPA2_AES_PSK, timeout)) {
    if (cyw43_arch_wifi_connect_timeout_ms("ZenWifi", "123indiozinhos", CYW43_AUTH_WPA2_AES_PSK, timeout)) {
        printf("failed to connect.\n");
        return false;
    } else {
        printf("Connected.\n");
    }
    return true;
}

/////////////////////////
// Main and Core1 Loop //
/////////////////////////
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
    speed_240_MHz = set_sys_clock_khz(240000, false);

    stdio_init_all();
    printf("Booting...\n");
    cyw43_arch_init();

    Delay_Timer(SEC(5));

    #ifdef DEBUG_SIGNAL_PINS
        gpio_init(9);
        gpio_set_dir(9, GPIO_OUT);
        gpio_put(9, false);

        gpio_init(10);
        gpio_set_dir(10, GPIO_OUT);
        gpio_put(10, false);
    #endif

    // For toggle_led
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

    #ifdef ERASE_EEPROM
        FormatFlashConfig();
    #endif

    Delay_Timer(SEC(2));
    
    memset(mobile->config_eeprom,0x00,sizeof(mobile->config_eeprom));
    ReadFlashConfig(mobile->config_eeprom, WiFiSSID, WiFiPASS); 

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

    #ifdef CONFIG_MODE
		char newSSID[28] = "WiFi_SSID";
		char newPASS[28] = "P@$$w0rd";

		char MAGB_DNS1[60] = "0.0.0.0";
		char MAGB_DNS2[60] = "0.0.0.0";
		unsigned MAGB_DNSPORT = 53;

		char RELAY_SERVER[60] = "0.0.0.0";
		unsigned P2P_PORT = 1027;

		char RELAY_TOKEN[32] = "00000000000000000000000000000000";
		bool updateRelayToken = false;

		bool DEVICE_UNMETERED = false;

        //Set the new values
        memcpy(WiFiSSID,newSSID,sizeof(newSSID));
        memcpy(WiFiPASS,newPASS,sizeof(newPASS));

        main_parse_addr(&dns1, MAGB_DNS1);
        main_parse_addr(&dns2, MAGB_DNS2);
        main_set_port(&dns1, MAGB_DNSPORT);
        main_set_port(&dns2, MAGB_DNSPORT);

        main_parse_addr(&relay, RELAY_SERVER);
        main_set_port(&relay, MOBILE_DEFAULT_RELAY_PORT);

        //Set the values to the Adapter config
        mobile_config_set_device(mobile->adapter, device, DEVICE_UNMETERED);
        mobile_config_set_dns(mobile->adapter, &dns1, &dns2);
        mobile_config_set_relay(mobile->adapter, &relay);
        mobile_config_set_p2p_port(mobile->adapter, P2P_PORT);

        if (updateRelayToken) {
            bool TokenOk = main_parse_hex(relay_token_buf, RELAY_TOKEN, sizeof(relay_token_buf));
            if(!TokenOk){
                printf("Invalid Relay Token\n");
            }else{
                mobile_config_set_relay_token(mobile->adapter, relay_token_buf);
            }
        }

        mobile_config_save(mobile->adapter);
        Delay_Timer(SEC(2));
        RefreshConfigBuff(mobile->config_eeprom,WiFiSSID,WiFiPASS);
        Delay_Timer(SEC(2));

        printf("New configuration defined! Please comment the \'#define CONFIG_MODE\' again to back the adapter to the normal operation.\n");
        while (1){
            LED_ON;
            sleep_ms(300);
            LED_OFF;
        }
    #endif

    isConnectedWiFi = PicoW_Connect_WiFi(WiFiSSID, WiFiPASS, MS(10));
    
    if(isConnectedWiFi){
        mobile->action = MOBILE_ACTION_NONE;
        mobile->number_user[0] = '\0';
        mobile->number_peer[0] = '\0';
        for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
            mobile->socket[i].tcp_pcb = NULL;
            mobile->socket[i].udp_pcb = NULL;
            mobile->socket[i].sock_addr = -1;
            mobile->socket[i].sock_type = -1;
            memset(mobile->socket[i].udp_remote_srv,0x00,sizeof(mobile->socket[i].udp_remote_srv));
            mobile->socket[i].udp_remote_port = 0;
            mobile->socket[i].client_status = false;
            memset(mobile->socket[i].buffer_rx,0x00,sizeof(mobile->socket[i].buffer_rx));
            memset(mobile->socket[i].buffer_tx,0x00,sizeof(mobile->socket[i].buffer_tx));
            mobile->socket[i].buffer_rx_len = 0;
            mobile->socket[i].buffer_tx_len = 0;
        } 

        multicore_launch_core1(core1_context);

        mobile_start(mobile->adapter);

        LED_OFF;
        while (true) {
            // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
            // main loop (not from a timer) to check for Wi-Fi driver or lwIP work that needs to be done.
            cyw43_arch_poll();
            // you can poll as often as you like, however if you have nothing else to do you can
            // choose to sleep until either a specified time, or cyw43_arch_poll() has work to do:
            cyw43_arch_wait_for_work_until(make_timeout_time_ms(1000));

            // Mobile Adapter Main Loop
            mobile_loop(mobile->adapter);

            // Check if there is any new config to write on Flash
            if(haveConfigToWrite){
                time_us_now_check = time_us_64();
                if (time_us_now_check - last_readable_check > SEC(5)){
                    if(!spi_is_readable(SPI_PORT) || !spiLock){
                        multicore_reset_core1();
                        Delay_Timer(SEC(2));
                        SaveFlashConfig(mobile->config_eeprom);
                        Delay_Timer(SEC(2));
                        haveConfigToWrite = false;
                        time_us_now_check = 0;
                        last_readable_check = 0;                    
                        multicore_launch_core1(core1_context);
                    }else{
                        last_readable_check = time_us_now_check;
                    }
                }
            }

            
        }
    }else{
        printf("Error during WiFi connection! =(\n");
        while(true){
            //do something
        }
    }
}