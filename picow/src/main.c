#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

#include "globals.h"
#include "mobile_data.h"

#include "storage/flash_eeprom.h"
#include "net/net_hal.h"
#include "web/web_server.h"
#include "core/adapter_bridge.h"
#include "core/led_status.h"
#include "pio/linkcable.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
bool speed_240_MHz = false;

//#define DEBUG_SIGNAL_PINS
#define CONFIG_LAST_EDIT_TIMEOUT SEC(1)
#define WIFI_CONNECT_TIMEOUT_MS 5000
#define WIFI_HOTSPOT_SSID "PicoAdapterGB"
#define WIFI_HOTSPOT_PASS "magb!123"
///////////////////////////////////////////////////////////////////////////////////////////////////

static bool web_alive = false;
static bool web_shutdown_pending = false;
static uint64_t web_shutdown_deadline = 0;

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
        memset(m->socket[i].udp_remote_ip, 0x00, sizeof(m->socket[i].udp_remote_ip));
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
    led_status_boot_start();
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
        // Wi-Fi not configured yet (still the defaults) isn't an error: it's
        // the expected first-boot state, so no error code is signaled for it.
        bool wifi_not_configured =
            (strcmp(mobile->wifiSSID, WIFI_DEFAULT_SSID) == 0 &&
            strcmp(mobile->wifiPASS, WIFI_DEFAULT_PASS) == 0) ||
            (mobile->wifiSSID[0] == '\0' &&
            mobile->wifiPASS[0] == '\0');
        if (wifi_not_configured) {
            led_status_report_error(net_wifi_last_connect_was_badauth()
                ? LED_ERROR_WIFI_BADAUTH : LED_ERROR_WIFI_CONNECT_FAILED);
        }

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

    web_alive = true;
    web_shutdown_pending = false;

    // The web setup UI is reachable from boot until the Game Boy starts
    // talking; core1 watches for that and signals core0 (the sole lwIP
    // owner) to tear it down. It never comes back until reboot.
    web_config_start(mobile);

    DEBUG_PRINT_FUNCTION("Web Setup available at http://%s/", net_wifi_ip_string());

    DEBUG_PRINT_FUNCTION("Initializing Game Boy link cable...");
    linkcable_init(link_cable_ISR);
    DEBUG_PRINT_FUNCTION("Game Boy link cable initialized.");

    DEBUG_PRINT_FUNCTION("Starting libmobile...");
    mobile_start(mobile->adapter);
    DEBUG_PRINT_FUNCTION("libmobile started.");

    // Normal boot is complete: hand the LED over to the runtime "config to
    // save" indicator (impl_config_write() / the auto-save block below).
    led_status_boot_done();

    bool first_main_loop = true;
    bool first_mobile_loop = true;
    while (true) {
        bool gameboy_session_active = mobile->adapter->commands.session_started;

        // During setup, service the web server first so a background relay
        // lookup cannot make the configuration page appear frozen. Once the
        // Game Boy starts a session, mobile_loop gets priority on every pass.
        if (!gameboy_session_active) net_poll();

        if (first_mobile_loop) {
            DEBUG_PRINT_FUNCTION("Entering first mobile loop...");
            first_mobile_loop = false;
        }
        mobile_loop(mobile->adapter);

        // lwIP remains necessary for relay/P2P sockets after a session starts.
        net_poll();
        web_config_service_pending_actions();
        if (first_main_loop) {
            DEBUG_PRINT_FUNCTION("Web/network polling is active.");
            first_main_loop = false;
        }

        if (web_alive && mobile->adapter->commands.session_started && !web_shutdown_pending) {
            // Let the Start Session response and the following handshake finish
            // before closing the unrelated HTTP connections.
            web_shutdown_pending = true;
            web_shutdown_deadline = time_us_64() + MS(1000);
            DEBUG_PRINT_FUNCTION("Game Boy session started; delaying Web Setup shutdown.");
        }

        if (web_alive && web_shutdown_pending && time_us_64() >= web_shutdown_deadline) {
            web_config_stop();
            web_alive = false;
            web_shutdown_pending = false;
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
                if (SaveConfig(&save_ptrs)) {
                    LED_OFF;
                } else {
                    // Leave the LED on (see led_status_report_error) instead of
                    // clearing it, since the config still isn't safely on flash.
                    led_status_report_error(LED_ERROR_FLASH_SAVE_FAILED);
                }
                adapter_bridge_clear_pending_config_write();
                mobile->force_save = false;
            }
        }
    }
}
