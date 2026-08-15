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

#include "globals.h"

#include "storage/flash_eeprom.h"
#include "net/net_hal.h"
#include "web/web_server.h"
#include "core/adapter_bridge.h"
#include "pio/linkcable.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
bool speed_240_MHz = false;

//#define DEBUG_SIGNAL_PINS
#define CONFIG_LAST_EDIT_TIMEOUT SEC(1)
#define WIFI_CONNECT_TIMEOUT_MS 5000
#define WIFI_DEFAULT_SSID "WiFi_Network"
#define WIFI_DEFAULT_PASS "P@$$w0rd"
#define WIFI_HOTSPOT_SSID "PicoAdapterGB"
#define WIFI_HOTSPOT_PASS "magb!123"
///////////////////////////////////////////////////////////////////////////////////////////////////

// Shared between the Game Boy ISR on core0 and the web shutdown watchdog on
// core1. It must remain volatile so release builds do not cache stale values
// across the two cores.
volatile bool link_cable_data_received = false;

// Set by core1 the instant the Game Boy starts any communication, so the web
// server can be torn down immediately and never re-enabled until reboot.
static volatile bool web_should_stop = false;
static bool web_alive = false;

struct mobile_user *mobile = NULL;

//////////////////////////
// LINK CABLE FUNCTIONS //
//////////////////////////

void TIME_SENSITIVE(link_cable_ISR)(void) {
    uint32_t data;
    if (isLinkCable32){
        data = mobile_transfer_32bit(mobile->adapter, linkcable_receive());
    } else {
        data = mobile_transfer(mobile->adapter, linkcable_receive());
    }
    linkcable_flush();
    linkcable_send(data);
    if (!link_cable_data_received) {
        link_cable_data_received = true;
    }
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

static void mobile_validate_relay(void){
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
    } else {
        LED_OFF;
    }
}

static void mobile_user_reset_runtime_state(struct mobile_user *m){
    m->action = MOBILE_ACTION_NONE;
    m->number_user[0] = '\0';
    m->number_peer[0] = '\0';
    m->currentReqSocket = -1;
    for (int i = 0; i < MOBILE_MAX_TIMERS; i++) m->picow_clock_latch[i] = 0;
    for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
        m->socket[i].tcp_pcb = NULL;
        m->socket[i].udp_pcb = NULL;
        m->socket[i].sock_addr = -1;
        m->socket[i].sock_type = SOCK_NONE;
        memset(m->socket[i].udp_remote_srv, 0x00, sizeof(m->socket[i].udp_remote_srv));
        m->socket[i].udp_remote_port = 0;
        m->socket[i].client_status = false;
        m->socket[i].inside_callback = false;
        m->socket[i].pending_close = false;
        m->socket[i].socket_status = 0;
        memset(m->socket[i].buffer_rx, 0x00, sizeof(m->socket[i].buffer_rx));
        m->socket[i].buffer_rx_len = 0;
        m->socket[i].buffer_tx_len = 0;
    }
    m->automatic_save = true;
    m->force_save = false;
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
    net_init();
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

    mobile = malloc(sizeof(struct mobile_user));
    memset(mobile, 0, sizeof(*mobile));
    strcpy(mobile->wifiSSID, WIFI_DEFAULT_SSID);
    strcpy(mobile->wifiPASS, WIFI_DEFAULT_PASS);

    InitSave();
    struct saved_data_pointers ptrs;
    InitSavedPointers(&ptrs, mobile);
    ReadConfig(&ptrs);

    mobile->adapter = mobile_new(mobile);
    adapter_bridge_register_callbacks(mobile->adapter);
    mobile_config_load(mobile->adapter);

    printf("-------------------------\nSoftware Version:\nLibmobile: %i.%i.%i\nPicoAdapterGB: %s-%s %s\n-------------------------\n",
        mobile_version_major, mobile_version_minor, mobile_version_patch,
        PICO_ADAPTER_HARDWARE, PICO_ADAPTER_PINOUT, PICO_ADAPTER_SOFTWARE);

    bool isConnectedWiFi = net_wifi_connect(mobile->wifiSSID, mobile->wifiPASS, WIFI_CONNECT_TIMEOUT_MS);

    if (!isConnectedWiFi) {
        // No usable WiFi: fall back to our own hotspot so the device can still
        // be reached and configured. There's nothing useful to continue with,
        // so this blocks forever; only a reboot (from the web page) gets out.
        DEBUG_PRINT_FUNCTION("Could not connect to WiFi. Starting hotspot \"%s\"...", WIFI_HOTSPOT_SSID);
        net_wifi_start_ap(WIFI_HOTSPOT_SSID, WIFI_HOTSPOT_PASS);
        DEBUG_PRINT_FUNCTION("Hotspot up. Connect to \"%s\" and open http://192.168.4.1/", WIFI_HOTSPOT_SSID);
        web_config_run_blocking(mobile);
        return; // unreachable: web_config_run_blocking never returns
    }

    mobile_user_reset_runtime_state(mobile);

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

    DEBUG_PRINT_FUNCTION("Web Setup available at http://%s/", net_wifi_ip_string());
    multicore_launch_core1(core1_web_killswitch);

    linkcable_init(link_cable_ISR);

    mobile_start(mobile->adapter);

    mobile_validate_relay();

    while (true) {
        // Mobile Adapter Main Loop
        mobile_loop(mobile->adapter);
        net_poll();

        if (web_alive && web_should_stop) {
            web_config_stop();
            web_alive = false;
            DEBUG_PRINT_FUNCTION("Game Boy communication detected, Web Setup server stopped.");
            DEBUG_PRINT_FUNCTION("WiFi status: %s", net_wifi_status_string());
        }

        net_service_pending_socket_closes(mobile);

        // Check if there is any new config to write on Flash
        if ((adapter_bridge_has_pending_config_write() && mobile->automatic_save) || mobile->force_save) {
            bool can_disable_irqs = can_disable_linkcable_handler();
            user_time_t curr_time_last_config_edit = adapter_bridge_last_config_edit_time();
            if (((TIME_FUNCTION - curr_time_last_config_edit) >= CONFIG_LAST_EDIT_TIMEOUT) && can_disable_irqs) {
                struct saved_data_pointers save_ptrs;
                InitSavedPointers(&save_ptrs, mobile);
                SaveConfig(&save_ptrs);
                adapter_bridge_clear_pending_config_write();
                mobile->force_save = false;
                LED_OFF;
            }
        }
    }
}
