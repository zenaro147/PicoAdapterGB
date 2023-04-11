////////////////////////////////////
// -- MOBILE_ENABLE_NO32BIT - try to build with this option enabled to handle GBA games
////////////////////////////////////
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include <mobile.h>
#include <mobile_inet.h>

#include "gblink.h"
#include "flash_eeprom.h"
#include "picow_socket.h"
#include "socket_impl.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
bool speed_240_MHz = false;

//#define DEBUG_SIGNAL_PINS

//#define ERASE_EEPROM //Encomment this to ERASE ALL stored config (including Adapter config)
//#define CONFIG_MODE //Uncomment this if you want to reconfigure something

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

//Time Config
#define MKS(A)                  (A)
#define MS(A)                   ((A) * 1000)
#define SEC(A)                  ((A) * 1000 * 1000)
volatile uint64_t time_us_now = 0;
uint64_t last_readable = 0;

///////////////////////////////////////
// Mobile Adapter GB Functions
///////////////////////////////////////
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

////////////////////////
// Auxiliar functions
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

///////////////////////////////////////
// Main Functions and Core 1 Loop
///////////////////////////////////////
#ifdef CONFIG_MODE
void SetNewConfig(){
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
    RefreshConfigBuff(mobile->config_eeprom);

    printf("New configuration defined! Please comment the \'#define CONFIG_MODE\' again to back the adapter to the normal operation.\n");
    while (1){
        LED_ON;
        Delay_Timer(MS(300));
        LED_OFF;
    }
}
#endif

bool PicoW_Connect_WiFi(char *ssid, char *psk, uint32_t timeout){
    cyw43_arch_enable_sta_mode();

    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(ssid, psk, CYW43_AUTH_WPA2_AES_PSK, timeout)) {
        printf("failed to connect.\n");
        return false;
    } else {
        printf("Connected.\n");
    }
    return true;
}

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
    if (!stdio_init_all() && cyw43_arch_init()) {
        printf("failed to initialise\n");
    }else{
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

        #ifdef ERASE_EEPROM
            FormatFlashConfig();
        #endif

        memset(mobile->config_eeprom,0x00,sizeof(mobile->config_eeprom));
        ReadFlashConfig(mobile->config_eeprom, WiFiSSID, WiFiPASS); 

        // Initialize mobile library
        mobile->adapter = mobile_new(mobile);
        // mobile_def_debug_log(mobile->adapter, impl_debug_log);
        // mobile_def_serial_disable(mobile->adapter, impl_serial_disable);
        // mobile_def_serial_enable(mobile->adapter, impl_serial_enable);
        // mobile_def_config_read(mobile->adapter, impl_config_read);
        // mobile_def_config_write(mobile->adapter, impl_config_write);
        // mobile_def_time_latch(mobile->adapter, impl_time_latch);
        // mobile_def_time_check_ms(mobile->adapter, impl_time_check_ms);
        // mobile_def_sock_open(mobile->adapter, impl_sock_open);
        // mobile_def_sock_close(mobile->adapter, impl_sock_close);
        // mobile_def_sock_connect(mobile->adapter, impl_sock_connect);
        // mobile_def_sock_listen(mobile->adapter, impl_sock_listen);
        // mobile_def_sock_accept(mobile->adapter, impl_sock_accept);
        // mobile_def_sock_send(mobile->adapter, impl_sock_send);
        // mobile_def_sock_recv(mobile->adapter, impl_sock_recv);
        // mobile_def_update_number(mobile->adapter, impl_update_number);

        mobile_config_load(mobile->adapter);

        #ifdef CONFIG_MODE
            SetNewConfig();
        #endif

        isConnectedWiFi = PicoW_Connect_WiFi(WiFiSSID, WiFiPASS, MS(10));
        
        if(isConnectedWiFi){
            mobile->action = MOBILE_ACTION_NONE;
            mobile->number_user[0] = '\0';
            mobile->number_peer[0] = '\0';
            for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
                //mobile->esp_sockets[i].host_id = -1;
            } 

            multicore_launch_core1(core1_context);

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
            printf("Error during WiFi connection! =(\n");
            while(true){
                //do something
            }
        }
    }
}