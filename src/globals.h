#pragma once

#include <mobile.h>
// struct mobile_user only ever holds an opaque per-connection handle (see
// net/socket_hal.h); the concrete socket_impl type is owned entirely by the
// selected implementation (src/implementations/<impl>/net/).
#include "net/socket_hal.h"
// LED_* macros below resolve to led_hal_*(); the concrete GPIO/peripheral is
// owned by the selected implementation. Keep this header self-sufficient
// instead of relying on whoever includes it first.
#include "core/led_hal.h"
// __not_in_flash_func()/TIME_SENSITIVE() and time_us_64()/TIME_FUNCTION below
// come from the Pico SDK's base and timer headers, common to RP2040 and
// RP2350 with no implementation-specific (cyw43/lwIP/etc.) dependency.
// (pico/platform.h itself must not be included directly - pico.h pulls it in.)
#include "pico.h"
#include "pico/time.h"

// PICO_ADAPTER_HARDWARE is injected by the build (see root CMakeLists.txt:
// it's derived from PICO_BOARD + PICOADAPTER_IMPLEMENTATION so this header
// never has to guess which implementation it's being compiled for).
#ifndef PICO_ADAPTER_HARDWARE
    #define PICO_ADAPTER_HARDWARE "Unknown"
#endif

#ifdef STACKSMASHING
    #define PICO_ADAPTER_PINOUT "StackSmashing"
#else
    #define PICO_ADAPTER_PINOUT "REON"
#endif

#ifndef PICO_ADAPTER_SOFTWARE
    #define PICO_ADAPTER_SOFTWARE "NO-VERSION"
#endif

#define WIFI_DEFAULT_SSID "WiFi_Network"
#define WIFI_DEFAULT_PASS "P@$$w0rd"

//#define DEBUG_SIGNAL_PINS

// WIFI SSID AND PASSWORD LENGHT
#define SSID_LENGHT 33 //32 chars + 1 to not overflow
#define PASS_LENGHT 64 //63 chars + 1 to not overflow

//LED Config
#define LED_SET(A)              (led_hal_set(A))
#define LED_ON                  LED_SET(true)
#define LED_OFF                 LED_SET(false)
#define LED_TOGGLE              (led_hal_set(!led_hal_get()))

//Time Config
#define MKS(A)                  (A)
#define MS(A)                   ((A) * 1000)
#define SEC(A)                  ((A) * 1000 * 1000)
#define TIME_FUNCTION           time_us_64()
typedef uint64_t                user_time_t;

//Time Sensitive functions
#define TIME_SENSITIVE(x) __not_in_flash_func(x)

#ifndef TIME_SENSITIVE
    #define TIME_SENSITIVE(x) x
#endif

#define DEBUG_PRINT_FUNCTION(fmt, ...) printf("[PicoAdapterGB] " fmt "\n", ##__VA_ARGS__)
#define EEPROM_SIZE MOBILE_CONFIG_SIZE

struct mobile_user {
    struct mobile_adapter *adapter;
    enum mobile_action action;
    unsigned long clock_latch[MOBILE_MAX_TIMERS];
    uint8_t config_eeprom[EEPROM_SIZE];
    char wifiSSID[SSID_LENGHT];
    char wifiPASS[PASS_LENGHT];
    // Opaque per-connection handles; see net/socket_hal.h. Populated once at
    // boot by socket_hal_bind() and owned by the selected implementation.
    struct socket_impl *socket[MOBILE_MAX_CONNECTIONS];
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
    bool automatic_save;
    bool force_save;
};
