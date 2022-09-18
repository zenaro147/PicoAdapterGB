#ifndef MAGB_COMMON_H
#define MAGB_COMMON_H

#include "libmobile/mobile.h"
#include "libmobile/inet_pton.h"

#define FLASH_DATA_SIZE (FLASH_PAGE_SIZE * 2)

#define LED_PIN         25
#define LED_SET(A)      (gpio_put(LED_PIN, (A)))
#define LED_ON          LED_SET(true)
#define LED_OFF         LED_SET(false)
#define LED_TOGGLE      (gpio_put(LED_PIN, !gpio_get(LED_PIN)))

#define MKS(A)                  (A)
#define MS(A)                   ((A) * 1000)
#define SEC(A)                  ((A) * 1000 * 1000)



struct esp_sock_config {
    int host_id;
    uint8_t host_type; //0=NONE, 1=TCP or 3=UDP
    enum mobile_addrtype host_iptype; //IPV4, IPV6 or NONE
    int local_port;
    bool sock_status;
};

struct mobile_user {
    struct mobile_adapter adapter;
    enum mobile_action action;
    uint8_t config_eeprom[FLASH_DATA_SIZE];
    struct esp_sock_config esp_sockets[MOBILE_MAX_CONNECTIONS];
};
struct mobile_user *mobile;

#endif