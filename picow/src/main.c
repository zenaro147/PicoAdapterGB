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
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"

#include "globals.h"

#include <mobile_inet.h>

#include "config_menu.h"
#include "flash_eeprom.h"
#include "picow_socket.h"
#include "pio/linkcable.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
bool speed_240_MHz = false;

//#define DEBUG_SIGNAL_PINS
#define CONFIG_LAST_EDIT_TIMEOUT SEC(1)
///////////////////////////////////////////////////////////////////////////////////////////////////

//Wi-Fi Controllers
bool isConnectedWiFi = false;

//Control Flash Write
bool haveConfigToWrite = false;
static user_time_t time_last_config_edit = 0;

bool isLinkCable32 = false;
bool link_cable_data_received = false;

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
    link_cable_data_received = true;
}

///////////////////////////////
// PICO W AUXILIAR FUNCTIONS //
///////////////////////////////
bool PicoW_Connect_WiFi(char *ssid, char *psk, uint32_t timeout){

    cyw43_pm_value(CYW43_NO_POWERSAVE_MODE,200,1,1,10);
    cyw43_arch_enable_sta_mode();
    
    //printf("Connecting to Wi-Fi... SSID: %s -- Password: %s [end line]\n", ssid, psk);
    int errorcode = cyw43_arch_wifi_connect_timeout_ms(ssid, psk, CYW43_AUTH_WPA2_AES_PSK, timeout);
    if (errorcode != 0) {
        printf("Failed to connect. Error: %i\n", errorcode);
        return false;
    } else {
        printf("Device IP: %s\nConnected.\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));
    }
    return true;
}

bool check_and_reconnect_wifi(char *ssid, char *psk, uint32_t timeout) {
    int errorcode = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (errorcode != CYW43_LINK_UP) {
        printf("Wi-Fi disconnected. Reconnecting...\n");
        
        cyw43_arch_disable_sta_mode();
        sleep_ms(1000);  // Espera um pouco antes de tentar de novo
        cyw43_pm_value(CYW43_NO_POWERSAVE_MODE,200,1,1,10);
        cyw43_arch_enable_sta_mode();

        int errorcode = cyw43_arch_wifi_connect_timeout_ms(ssid, psk, CYW43_AUTH_WPA2_AES_PSK, timeout);
        if (errorcode != 0) {
            printf("Reconnect failed: %i\n", errorcode);
            return false;
        } else {
            printf("Wi-Fi reconnected. IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));
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
    speed_240_MHz = set_sys_clock_khz(240000, false);

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
    memset(mobile->config_eeprom,0x00,sizeof(mobile->config_eeprom));
    strcpy(mobile->wifiSSID, "WiFi_Network");
    strcpy(mobile->wifiPASS, "P@$$w0rd");

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

    BootMenuConfig(mobile);

    printf("-------------------------\nSoftware Version:\nLibmobile: %i.%i.%i\nPicoAdapterGB: %s-%s %s\n-------------------------\n",mobile_version_major,mobile_version_minor,mobile_version_patch,PICO_ADAPTER_HARDWARE,PICO_ADAPTER_PINOUT,PICO_ADAPTER_SOFTWARE);

    isConnectedWiFi = PicoW_Connect_WiFi(mobile->wifiSSID, mobile->wifiPASS, MS(60));
    
    if(isConnectedWiFi){
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
            mobile->socket[i].pending_close = false;
            memset(mobile->socket[i].buffer_rx,0x00,sizeof(mobile->socket[i].buffer_rx));
            //memset(mobile->socket[i].buffer_tx,0x00,sizeof(mobile->socket[i].buffer_tx));
            mobile->socket[i].buffer_rx_len = 0;
            mobile->socket[i].buffer_tx_len = 0;
        } 
        mobile->automatic_save = true;
        mobile->force_save = false;

        // multicore_launch_core1(core1_context);

        linkcable_init(link_cable_ISR);

        mobile_start(mobile->adapter);

        mobile_validate_relay();


        while (true) {
            // Mobile Adapter Main Loop
            mobile_loop(mobile->adapter);
            cyw43_arch_poll();
            
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
    }else{
        printf("Error during WiFi connection!\n");
        while(true){
            LED_ON;
            busy_wait_us(MS(300));
            LED_OFF;
            busy_wait_us(MS(300));
        }
    }
}
