#pragma once

#include <mobile.h>
#include "socket_impl.h"



#ifdef BOARD_PICO2W
    #define PICO_ADAPTER_HARDWARE "Pico2W"
#else
    #ifdef BOARD_PICOW
         #define PICO_ADAPTER_HARDWARE "PicoW"
    #else
         #define PICO_ADAPTER_HARDWARE "Pico+ESP"
    #endif   
#endif

#ifdef STACKSMASHING
    #define PICO_ADAPTER_PINOUT "StackSmashing"
#else    
    #define PICO_ADAPTER_PINOUT "REON"
#endif

#define PICO_ADAPTER_SOFTWARE "1.5.5-beta"

//#define DEBUG_SIGNAL_PINS

// WIFI SSID AND PASSWORD LENGHT
#define SSID_LENGHT 33 //32 chars + 1 to not overflow
#define PASS_LENGHT 64 //63 chars + 1 to not overflow

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
#define TIME_FUNCTION           time_us_64()
typedef uint64_t                user_time_t;

//Time Sensitive functions
#define TIME_SENSITIVE(x) __not_in_flash_func(x)

#ifndef TIME_SENSITIVE
#define TIME_SENSITIVE(x) x
#endif

#define DEBUG_PRINT_FUNCTION(x) printf(x)
#define EEPROM_SIZE MOBILE_CONFIG_SIZE

struct mobile_user {
    struct mobile_adapter *adapter;
    enum mobile_action action;
    uint8_t currentReqSocket;
    unsigned long picow_clock_latch[MOBILE_MAX_TIMERS];
    uint8_t config_eeprom[EEPROM_SIZE];
    char wifiSSID[SSID_LENGHT];
    char wifiPASS[PASS_LENGHT];
    struct socket_impl socket[MOBILE_MAX_CONNECTIONS];
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
    bool automatic_save;
    bool force_save;
};
