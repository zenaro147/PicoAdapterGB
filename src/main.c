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
#include "net/socket_hal.h"
#include "core/adapter_bridge.h"
#include "core/led_status.h"
#include "pio/linkcable.h"

// Web setup UI is optional: not every implementation provides one (e.g. a
// future 4G/modem backend has nothing for a browser to talk to). Set by the
// selected implementation's CMakeLists.txt when it links a web/ module that
// implements this contract (web_config_start/stop/run_blocking/
// service_pending_actions). main.c never falls back to a dummy web backend;
// it simply skips every web_config_*() call when this isn't defined.
#ifdef PICOADAPTER_HAS_WEB
#include "web/web_server.h"
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////
bool speed_240_MHz = false;

//#define DEBUG_SIGNAL_PINS
#define CONFIG_LAST_EDIT_TIMEOUT SEC(1)
#define WIFI_CONNECT_TIMEOUT_MS 5000
#define WIFI_HOTSPOT_SSID "PicoAdapterGB"
#define WIFI_HOTSPOT_PASS "magb!123"
///////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef PICOADAPTER_HAS_WEB
static bool web_alive = false;
static bool web_shutdown_pending = false;
static uint64_t web_shutdown_deadline = 0;
#endif

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
    for (int i = 0; i < MOBILE_MAX_TIMERS; i++) m->clock_latch[i] = 0;
    // Concrete per-connection state is owned by the selected implementation
    // (see net/socket_hal.h); this only asks it to reset each handle.
    for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++) socket_hal_reset(m->socket[i]);
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
    if (!net_init()) {
        // Network hardware/module never came up - e.g. the esp
        // implementation's ESP-AT module not responding on UART (wiring,
        // power, or a firmware/baud rate mismatch - see
        // src/implementations/esp/README.md). There's nothing safe to fall
        // back to: Wi-Fi credentials might be fine, but there's no way to
        // try them, and the setup hotspot needs working network hardware
        // too. Halt here instead of falling through into a Wi-Fi connect
        // attempt that's doomed to fail and would surface later as a
        // confusing "Wi-Fi connection failed" instead of the real cause.
        //
        // net_init() must still run before any led_status_*() call: on
        // picow the LED is wired through the CYW43 chip itself (see
        // core/led_hal.h) and is only drivable once cyw43_arch_init(),
        // called from net_init(), has actually succeeded - so if THAT is
        // what just failed, this blink pattern may not be visible there.
        // DEBUG_PRINT_FUNCTION is the fallback diagnostic in that case. On
        // esp, the LED is a plain GPIO independent of ESP-AT, so it's
        // reliable here.
        DEBUG_PRINT_FUNCTION("Network hardware failed to initialize. Halting.");
        led_status_boot_start();
        while (true) led_status_report_error(LED_ERROR_NET_INIT_FAILED);
    }
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
    // Wires up mobile->socket[] with implementation-owned handles. Must run
    // before adapter_bridge_register_callbacks(), since a Game Boy session
    // could in principle ask for a socket as soon as libmobile is started.
    socket_hal_bind(mobile);

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

#ifdef PICOADAPTER_HAS_WEB
        // No usable WiFi: fall back to our own hotspot so the device can still
        // be reached and configured. There's nothing useful to continue with,
        // so this blocks forever; only a reboot (from the web page) gets out.
        DEBUG_PRINT_FUNCTION("Could not connect to WiFi. Starting hotspot \"%s\"...", WIFI_HOTSPOT_SSID);
        net_wifi_start_ap(WIFI_HOTSPOT_SSID, WIFI_HOTSPOT_PASS);
        DEBUG_PRINT_FUNCTION("Hotspot up. Connect to \"%s\" and open http://192.168.4.1/", WIFI_HOTSPOT_SSID);

        web_config_run_blocking(mobile);
        return; // unreachable: web_config_run_blocking never returns
#else
        // No web setup UI in this build: nothing a user could reach to fix
        // Wi-Fi credentials, so there's nothing useful left to do.
        DEBUG_PRINT_FUNCTION("Could not connect to WiFi and this build has no web setup UI.");
        return;
#endif
    }

    mobile_user_reset_runtime_state(mobile);

#ifdef PICOADAPTER_HAS_WEB
    web_alive = true;
    web_shutdown_pending = false;
#endif

    DEBUG_PRINT_FUNCTION("Initializing Game Boy link cable...");
    linkcable_init(link_cable_ISR);
    DEBUG_PRINT_FUNCTION("Game Boy link cable initialized.");

    DEBUG_PRINT_FUNCTION("Starting libmobile...");
    mobile_start(mobile->adapter);
    DEBUG_PRINT_FUNCTION("libmobile started.");

#ifdef PICOADAPTER_HAS_WEB
    // The web setup UI is reachable from boot until the Game Boy starts
    // talking; the main loop watches for that below and tears it down.
    // It never comes back until reboot.
    web_config_start(mobile);
    DEBUG_PRINT_FUNCTION("Web Setup available at http://%s/", net_wifi_ip_string());
#endif

    bool first_main_loop = true;
    bool first_mobile_loop = true;
    while (true) {
        bool gameboy_session_active = mobile->adapter->commands.session_started;

        // During setup, service the web server first so a background relay
        // lookup cannot make the configuration page appear frozen. Once the
        // Game Boy starts a session, mobile_loop gets priority on every pass.
        if (!gameboy_session_active) net_poll();

        if (first_mobile_loop) {
            // Normal boot is complete: hand the LED over to the runtime "config to
            // save" indicator (impl_config_write() / the auto-save block below).
            led_status_boot_done();
            DEBUG_PRINT_FUNCTION("Entering first mobile loop...");
            first_mobile_loop = false;
        }
        mobile_loop(mobile->adapter);

        // lwIP remains necessary for relay/P2P sockets after a session starts.
        net_poll();
#ifdef PICOADAPTER_HAS_WEB
        web_config_service_pending_actions();
#endif
        if (first_main_loop) {
            DEBUG_PRINT_FUNCTION("Web/network polling is active.");
            first_main_loop = false;
        }

#ifdef PICOADAPTER_HAS_WEB
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
#endif

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
