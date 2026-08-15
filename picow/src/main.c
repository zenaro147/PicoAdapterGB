////////////////////////////////////
// -- MOBILE_ENABLE_NO32BIT - try to build with this option enabled to handle GBA games
// -- /usr/local/bin/openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg
// -- cd "/media/rafael/Dados/_BACKUP/Arquivos/Projetos/Gameboy Projects/MobileAdapterGB/libmobile-bgb" \
// -- && build/mobile --dns1 18.223.26.183 --unmetered --relay 192.168.1.9 --relay-token "A96F8F0226A2E6C4A2C13689413BB09E"
////////////////////////////////////
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"

#include "globals.h"

#include <mobile_inet.h>

#include "flash_eeprom.h"
#include "picow_socket.h"
#include "web_config.h"
#include "pio/linkcable.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
bool speed_240_MHz = false;

//#define DEBUG_SIGNAL_PINS
#define CONFIG_LAST_EDIT_TIMEOUT SEC(1)
///////////////////////////////////////////////////////////////////////////////////////////////////

//Wi-Fi Controllers
bool isConnectedWiFi = false;
#define WIFI_CONNECT_MAX_ATTEMPTS 5
#define WIFI_CONNECT_TIMEOUT_MS 5000
#define WIFI_CONNECT_RETRY_DELAY_MS 1000
#define WIFI_DEFAULT_SSID "WiFi_Network"
#define WIFI_DEFAULT_PASS "P@$$w0rd"

//Control Flash Write
bool haveConfigToWrite = false;
static user_time_t time_last_config_edit = 0;

bool isLinkCable32 = false;
// Shared between the Game Boy ISR on core0 and the web shutdown watchdog on
// core1. It must remain volatile so release builds do not cache stale values
// across the two cores.
volatile bool link_cable_data_received = false;

// Set by core1 the instant the Game Boy starts any communication, so the web
// server can be torn down immediately and never re-enabled until reboot.
static volatile bool web_should_stop = false;
static bool web_alive = false;

/////////////////////////////////
// MOBILE ADAPTER GB FUNCTIONS //
/////////////////////////////////

struct mobile_user *mobile = NULL;

static void impl_debug_log(void *user, const char *line){
    (void)user;
    fprintf(stderr, "%s\n", line);
}

static void impl_serial_disable(void *user) {
    #ifdef DEBUG_SIGNAL_PINS
        gpio_put(10, true);
    #endif
    struct mobile_user *mobile = (struct mobile_user *)user;
  
    linkcable_reset(false);   
    // spi_deinit(SPI_PORT);    
}

static void impl_serial_enable(void *user, bool mode_32bit) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    
    isLinkCable32 = mode_32bit;
    linkcable_set_is_32(mode_32bit);
    
    #ifdef DEBUG_SIGNAL_PINS
        gpio_put(10, false);
    #endif
    linkcable_enable();
}

static bool impl_config_read(void *user, void *dest, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for(int i = 0; i < size; i++){
        ((char *)dest)[i] = (char)mobile->config_eeprom[offset + i];
    }
    return true;
}

static bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    const uint8_t* src_8 = (const uint8_t *)src;
    bool this_edited_config = false;
    for(int i = 0; i < size; i++) {
        if(mobile->config_eeprom[offset + i] != src_8[i])
            this_edited_config = true;
        mobile->config_eeprom[offset + i] = src_8[i];
    }
    if(this_edited_config) {
        LED_ON;
        haveConfigToWrite = true;
        time_last_config_edit = TIME_FUNCTION;
    }
    return true;
}

static void impl_time_latch(void *user, unsigned timer) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->picow_clock_latch[timer] = time_us_64();
}

static bool impl_time_check_ms(void *user, unsigned timer, unsigned ms) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    return ((time_us_64() - mobile->picow_clock_latch[timer]) >= MS(ms));
}

//Callbacks
static bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_open\n");
    mobile->currentReqSocket = conn;
    return socket_impl_open(&mobile->socket[conn], socktype, addrtype, bindport, user);
}

static void impl_sock_close(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_close\n");
    mobile->currentReqSocket = conn;
    return socket_impl_close(&mobile->socket[conn]);
}

static int impl_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_connect\n"); 
    mobile->currentReqSocket = conn;
    return socket_impl_connect(&mobile->socket[conn], addr);
}

static int impl_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_send\n");
    mobile->currentReqSocket = conn;
    return socket_impl_send(&mobile->socket[conn], data, size, addr);
}

static int impl_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;    
    // printf("mobile_impl_sock_recv\n");
    mobile->currentReqSocket = conn;
    return socket_impl_recv(&mobile->socket[conn], data, size, addr);
}

static bool impl_sock_listen(void *user, unsigned conn){ 
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_listen\n");
    mobile->currentReqSocket = conn;
    return socket_impl_listen(&mobile->socket[conn],user);
}

static bool impl_sock_accept(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    // printf("mobile_impl_sock_accept\n"); 
    mobile->currentReqSocket = conn;
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

    LED_OFF;
}

//////////////////////////
// LINK CABLE FUNCTIONS //
//////////////////////////

void TIME_SENSITIVE(link_cable_ISR)(void) {
    uint32_t data;
    if(isLinkCable32){
        data = mobile_transfer_32bit(mobile->adapter, linkcable_receive());
    }else{
        data = mobile_transfer(mobile->adapter, linkcable_receive());
    }
    linkcable_flush();
    linkcable_send(data);
    if (!link_cable_data_received) {
        link_cable_data_received = true;
    }
}

///////////////////////////////
// PICO W AUXILIAR FUNCTIONS //
///////////////////////////////
static const char *wifi_link_status_str(int status){
    switch (status) {
        case CYW43_LINK_DOWN:    return "DOWN (wifi not connected)";
        case CYW43_LINK_JOIN:    return "JOIN (connected, no IP yet)";
        case CYW43_LINK_NOIP:    return "NOIP (connected, no IP address)";
        case CYW43_LINK_UP:      return "UP (connected with IP address)";
        case CYW43_LINK_FAIL:    return "FAIL (connection failed)";
        case CYW43_LINK_NONET:   return "NONET (SSID not found)";
        case CYW43_LINK_BADAUTH: return "BADAUTH (authentication failure)";
        default:                 return "UNKNOWN";
    }
}

bool PicoW_Connect_WiFi(char *ssid, char *psk, uint32_t timeout){

    if (strcmp(ssid, WIFI_DEFAULT_SSID) == 0 && strcmp(psk, WIFI_DEFAULT_PASS) == 0) {
        DEBUG_PRINT_FUNCTION("Wi-Fi credentials are still the defaults; skipping onnection attempts.");
        return false;
    }

    cyw43_pm_value(CYW43_NO_POWERSAVE_MODE,200,1,1,10);
    cyw43_arch_enable_sta_mode();
    
    //printf("Connecting to Wi-Fi... SSID: %s -- Password: %s [end line]\n", ssid, psk);
    for (unsigned attempt = 1; attempt <= WIFI_CONNECT_MAX_ATTEMPTS; attempt++) {
        int errorcode = cyw43_arch_wifi_connect_timeout_ms(
            ssid, psk, CYW43_AUTH_WPA2_AES_PSK, timeout);
        if (errorcode == 0) {
            DEBUG_PRINT_FUNCTION("Device IP: %s", ip4addr_ntoa(netif_ip4_addr(netif_list)));
            DEBUG_PRINT_FUNCTION("Connected on attempt %u.", attempt);
            return true;
        }

        DEBUG_PRINT_FUNCTION("Wi-Fi connection attempt %u/%u failed. Error: %i",
            attempt, WIFI_CONNECT_MAX_ATTEMPTS, errorcode);
        if (attempt < WIFI_CONNECT_MAX_ATTEMPTS) {
            sleep_ms(WIFI_CONNECT_RETRY_DELAY_MS);
        }
    }
    return false;
}

// Runs on core1 only while the web config server is alive. Watches for the
// Game Boy's first byte transfer and signals core0 (the sole owner of
// cyw43/lwIP) to tear the server down immediately. Never restarts itself.
// Must stay resident in RAM: core0 disables flash XIP while saving config
// (SaveConfig/flash_range_program), which would crash core1 if it were still
// fetching instructions from flash at that moment.
static void TIME_SENSITIVE(core1_web_killswitch)(void){
    while (!link_cable_data_received) {
        tight_loop_contents();
    }
    web_should_stop = true;
    while (true) {
        tight_loop_contents();
    }
}

bool check_and_reconnect_wifi(char *ssid, char *psk, uint32_t timeout) {
    int errorcode = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (errorcode != CYW43_LINK_UP) {
        DEBUG_PRINT_FUNCTION("Wi-Fi disconnected. Reconnecting...");
        
        cyw43_arch_disable_sta_mode();
        sleep_ms(1000);  // Espera um pouco antes de tentar de novo
        cyw43_pm_value(CYW43_NO_POWERSAVE_MODE,200,1,1,10);
        cyw43_arch_enable_sta_mode();

        int errorcode = cyw43_arch_wifi_connect_timeout_ms(ssid, psk, CYW43_AUTH_WPA2_AES_PSK, timeout);
        if (errorcode != 0) {
            DEBUG_PRINT_FUNCTION("Reconnect failed: %i", errorcode);
            return false;
        } else {
            DEBUG_PRINT_FUNCTION("Wi-Fi reconnected. IP: %s", ip4addr_ntoa(netif_ip4_addr(netif_list)));
            return true;
        }
    }
    return true;
}


void mobile_validate_relay(){
    struct mobile_addr relay = {0};    
    mobile_config_get_relay(mobile->adapter, &relay);
    if (relay.type != MOBILE_ADDRTYPE_NONE){
            for (int i = 0; i < 3; i++){
                LED_ON;
                busy_wait_us(MS(150));
                LED_OFF;
                busy_wait_us(MS(150));
            }
            LED_ON;
    } else{
        LED_OFF;
    }
}

/////////////////////////
// Main and Core1 Loop //
/////////////////////////
void main(){
    #ifndef PICO_CYW43_ARCH_POLL
        speed_240_MHz = set_sys_clock_khz(240000, false);
    #endif
    
    stdio_init_all();
    printf("Booting...\n");
    cyw43_arch_init();
    LED_ON;
    busy_wait_us(SEC(5));

    #ifdef DEBUG_SIGNAL_PINS
        gpio_init(9);
        gpio_set_dir(9, GPIO_OUT);
        gpio_put(9, false);

        gpio_init(10);
        gpio_set_dir(10, GPIO_OUT);
        gpio_put(10, false);
    #endif
    
    //Libmobile Variables
    mobile = malloc(sizeof(struct mobile_user));
    memset(mobile, 0, sizeof(*mobile));
    memset(mobile->config_eeprom,0x00,sizeof(mobile->config_eeprom));
        strcpy(mobile->wifiSSID, WIFI_DEFAULT_SSID);
        strcpy(mobile->wifiPASS, WIFI_DEFAULT_PASS);

    InitSave();
    struct saved_data_pointers ptrs;
    InitSavedPointers(&ptrs, mobile);
    ReadConfig(&ptrs);
    
    mobile->adapter = mobile_new(mobile);
    // Initialize mobile callbacks
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

    printf("-------------------------\nSoftware Version:\nLibmobile: %i.%i.%i\nPicoAdapterGB: %s-%s %s\n-------------------------\n",mobile_version_major,mobile_version_minor,mobile_version_patch,PICO_ADAPTER_HARDWARE,PICO_ADAPTER_PINOUT,PICO_ADAPTER_SOFTWARE);

    isConnectedWiFi = PicoW_Connect_WiFi(mobile->wifiSSID, mobile->wifiPASS, WIFI_CONNECT_TIMEOUT_MS);

    if (!isConnectedWiFi) {
        // No usable WiFi: fall back to our own hotspot so the device can still
        // be reached and configured. There's nothing useful to continue with,
        // so this blocks forever; only a reboot (from the web page) gets out.
        const char *WIFI_HOTSPOT_SSID = "PicoAdapterGB";
        const char *WIFI_HOTSPOT_PASS = "magb!123";
        DEBUG_PRINT_FUNCTION("Could not connect to WiFi. Starting hotspot \"PicoAdapterGB\"...", WIFI_HOTSPOT_SSID);
        cyw43_arch_disable_sta_mode();
        cyw43_arch_enable_ap_mode(WIFI_HOTSPOT_SSID, WIFI_HOTSPOT_PASS, CYW43_AUTH_WPA2_AES_PSK);
        DEBUG_PRINT_FUNCTION("Hotspot up. Connect to \"%s\" and open http://192.168.4.1/", WIFI_HOTSPOT_SSID);
        web_config_run_blocking(mobile);
        return; // unreachable: web_config_run_blocking never returns
    }
    
    mobile->action = MOBILE_ACTION_NONE;
    mobile->number_user[0] = '\0';
    mobile->number_peer[0] = '\0';
    mobile->currentReqSocket = -1;
    for (int i = 0; i < MOBILE_MAX_TIMERS; i++) mobile->picow_clock_latch[i] = 0;
    for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
        mobile->socket[i].tcp_pcb = NULL;
        mobile->socket[i].udp_pcb = NULL;
        mobile->socket[i].sock_addr = -1;
        mobile->socket[i].sock_type = SOCK_NONE;
        memset(mobile->socket[i].udp_remote_srv,0x00,sizeof(mobile->socket[i].udp_remote_srv));
        mobile->socket[i].udp_remote_port = 0;
        mobile->socket[i].client_status = false;
        mobile->socket[i].inside_callback = false;
        mobile->socket[i].pending_close = false;
        mobile->socket[i].socket_status = 0;
        memset(mobile->socket[i].buffer_rx,0x00,sizeof(mobile->socket[i].buffer_rx));
        //memset(mobile->socket[i].buffer_tx,0x00,sizeof(mobile->socket[i].buffer_tx));
        mobile->socket[i].buffer_rx_len = 0;
        mobile->socket[i].buffer_tx_len = 0;
    } 
    mobile->automatic_save = true;
    mobile->force_save = false;

    // Reset the shared cross-core gate before starting the web server. The
    // shutdown watchdog on core1 waits for the Game Boy ISR on core0 to set
    // link_cable_data_received; this reset keeps the startup state explicit.
    link_cable_data_received = false;
    web_should_stop = false;
    web_alive = true;

    // The web setup UI is reachable from boot until the Game Boy starts
    // talking; core1 watches for that and signals core0 (the sole lwIP
    // owner) to tear it down. It never comes back until reboot.
    web_config_start(mobile);
    
    DEBUG_PRINT_FUNCTION("Web Setup available at http://%s/", ip4addr_ntoa(netif_ip4_addr(netif_list)));
    multicore_launch_core1(core1_web_killswitch);

    linkcable_init(link_cable_ISR);

    mobile_start(mobile->adapter);

    mobile_validate_relay();

    while (true) {
        // Mobile Adapter Main Loop
        mobile_loop(mobile->adapter);
        cyw43_arch_poll();

        if (web_alive && web_should_stop) {
            web_config_stop();
            web_alive = false;
            DEBUG_PRINT_FUNCTION("Game Boy communication detected, Web Setup server stopped.");
            DEBUG_PRINT_FUNCTION("WiFi status: %s", wifi_link_status_str(cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA)));
        }
        
        for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
            // if(mobile->socket[i].tcp_pcb || mobile->socket[i].udp_pcb){                    
            //     cyw43_arch_poll();
            //     check_and_reconnect_wifi(mobile->wifiSSID, mobile->wifiPASS, MS(60));
            //     break;
            // }
            if (mobile->socket[i].pending_close) {
                socket_impl_close_commands(&mobile->socket[i]);
            }
        }

        // Check if there is any new config to write on Flash
        if((haveConfigToWrite && mobile->automatic_save) || mobile->force_save) {
            bool can_disable_irqs = can_disable_linkcable_handler();
            user_time_t curr_time_last_config_edit = time_last_config_edit;
            if(((TIME_FUNCTION - curr_time_last_config_edit) >= CONFIG_LAST_EDIT_TIMEOUT) && can_disable_irqs) {
                struct saved_data_pointers ptrs;
                InitSavedPointers(&ptrs, mobile);
                SaveConfig(&ptrs);
                haveConfigToWrite = false;
                mobile->force_save = false;
                LED_OFF;                    
            }
        }
    }
}
