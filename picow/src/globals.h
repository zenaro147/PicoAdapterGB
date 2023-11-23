#pragma once

#include <mobile.h>
#include "socket_impl.h"
#include "flash_eeprom.h"

#define PICO_ADAPTER_HARDWARE "PicoW"
#define PICO_ADAPTER_SOFTWARE "1.5.1-b"

//#define DEBUG_SIGNAL_PINS

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

//Time Sensitive functions
#define TIME_SENSITIVE(x) __not_in_flash_func(x)

#ifndef TIME_SENSITIVE
#define TIME_SENSITIVE(x) x
#endif

struct mobile_user {
    struct mobile_adapter *adapter;
    enum mobile_action action;
    uint8_t currentReqSocket;
    unsigned long picow_clock_latch[MOBILE_MAX_TIMERS];
    uint8_t config_eeprom[FLASH_DATA_SIZE];
    struct socket_impl socket[MOBILE_MAX_CONNECTIONS];
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
};
