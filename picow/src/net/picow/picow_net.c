// cyw43+lwIP implementation of the board-agnostic net_hal.h interface.
#include "../net_hal.h"
#include "picow_net.h"

#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "globals.h"
#include "socket_impl.h"

#define WIFI_CONNECT_MAX_ATTEMPTS 5
#define WIFI_CONNECT_RETRY_DELAY_MS 1000

static int last_connect_errorcode = 0;

bool net_init(void){
    return cyw43_arch_init() == 0;
}

bool net_wifi_connect(const char *ssid, const char *psk, uint32_t timeout_ms){
    last_connect_errorcode = 0;
    if (ssid[0] == '\0' && psk[0] == '\0') {
        DEBUG_PRINT_FUNCTION("Wi-Fi credentials are still the defaults; skipping connection attempts.");
        return false;
    }

    cyw43_pm_value(CYW43_NO_POWERSAVE_MODE, 200, 1, 1, 10);
    cyw43_arch_enable_sta_mode();

    for (unsigned attempt = 1; attempt <= WIFI_CONNECT_MAX_ATTEMPTS; attempt++) {
        int errorcode = cyw43_arch_wifi_connect_timeout_ms(
            (char *)ssid, (char *)psk, CYW43_AUTH_WPA2_AES_PSK, timeout_ms);
        last_connect_errorcode = errorcode;
        if (errorcode == 0) {
            DEBUG_PRINT_FUNCTION("Device IP: %s", net_wifi_ip_string());
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

bool net_wifi_last_connect_was_badauth(void){
    return last_connect_errorcode == PICO_ERROR_BADAUTH;
}

void net_wifi_start_ap(const char *ssid, const char *psk){
    cyw43_arch_disable_sta_mode();
    cyw43_arch_enable_ap_mode((char *)ssid, (char *)psk, CYW43_AUTH_WPA2_AES_PSK);
}

const char *net_wifi_status_string(void){
    switch (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA)) {
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

const char *net_wifi_ip_string(void){
    return ip4addr_ntoa(netif_ip4_addr(netif_list));
}

void net_poll(void){
    cyw43_arch_poll();
}

void net_service_pending_socket_closes(struct mobile_user *mobile){
    for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
        if (mobile->socket[i].pending_close) {
            socket_impl_close_commands(&mobile->socket[i]);
        }
    }
}
