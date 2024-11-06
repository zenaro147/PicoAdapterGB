#pragma once

#include <mobile.h>
#include "socket_impl.h"

#define PICO_ADAPTER_HARDWARE "PicoW"
#define PICO_ADAPTER_SOFTWARE "1.5.0-b"

//#define DEBUG_SIGNAL_PINS

//LED Config
#define LED_PIN       		  	CYW43_WL_GPIO_LED_PIN
#define LED_SET(A)    		  	(cgpio_put(LED_PIN, (A)))
#define LED_ON        		  	LED_SET(true)
#define LED_OFF       		  	LED_SET(false)
#define LED_TOGGLE    		  	(gpio_put(LED_PIN, !gpio_get(LED_PIN)))

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
    char wifiSSID[32];
    char wifiPASS[32];
    struct socket_impl socket[MOBILE_MAX_CONNECTIONS];
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
    bool automatic_save;
    bool force_save;
};