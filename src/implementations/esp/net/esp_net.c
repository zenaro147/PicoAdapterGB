// ESP-AT (UART + ESP8266EX) implementation of the board-agnostic net_hal.h
// interface. All protocol/command detail lives in esp_at.h/.c; this file only
// adapts that to the net_hal.h contract.
#include "net/net_hal.h"
#include "esp_at.h"
#include "esp_config.h"
#include "esp_device_auth_http.h"

#include <stdio.h>
#include <string.h>

#include "globals.h"

#define WIFI_CONNECT_MAX_ATTEMPTS 5
#define WIFI_CONNECT_RETRY_DELAY_MS 1000

static esp_wifi_join_result_t last_join_result = ESP_WIFI_JOIN_OK;

bool net_init(void){
    return esp_at_init();
}

bool net_wifi_connect(const char *ssid, const char *psk, uint32_t timeout_ms){
    if ((strcmp(ssid, WIFI_DEFAULT_SSID) == 0 && strcmp(psk, WIFI_DEFAULT_PASS) == 0) ||
        (ssid[0] == '\0' && psk[0] == '\0')) {
        DEBUG_PRINT_FUNCTION("Wi-Fi credentials are still the defaults; skipping connection attempts.");
        return false;
    }

    for (unsigned attempt = 1; attempt <= WIFI_CONNECT_MAX_ATTEMPTS; attempt++) {
        DEBUG_PRINT_FUNCTION("Starting Wi-Fi connection attempt %u/%u.", attempt, WIFI_CONNECT_MAX_ATTEMPTS);

        last_join_result = esp_at_wifi_join(ssid, psk, timeout_ms);

        if (last_join_result == ESP_WIFI_JOIN_OK) {
            DEBUG_PRINT_FUNCTION("Device IP: %s", net_wifi_ip_string());
            DEBUG_PRINT_FUNCTION("Connected on attempt %u.", attempt);
            return true;
        }

        if (last_join_result == ESP_WIFI_JOIN_BADAUTH) {
            DEBUG_PRINT_FUNCTION("Wi-Fi authentication failed.");
            return false; // not useful to retry with the same credentials
        }

        if (last_join_result == ESP_WIFI_JOIN_NO_AP) {
            DEBUG_PRINT_FUNCTION("Wi-Fi network not found.");
        } else if (last_join_result == ESP_WIFI_JOIN_MODULE_ERROR) {
            DEBUG_PRINT_FUNCTION("ESP module is not responding.");
        } else {
            DEBUG_PRINT_FUNCTION("Wi-Fi connection failed or timed out.");
        }

        if (attempt < WIFI_CONNECT_MAX_ATTEMPTS) sleep_ms(WIFI_CONNECT_RETRY_DELAY_MS);
    }

    return false;
}

bool net_wifi_last_connect_was_badauth(void){
    return last_join_result == ESP_WIFI_JOIN_BADAUTH;
}

void net_wifi_start_ap(const char *ssid, const char *psk){
    esp_at_wifi_start_ap(ssid, psk);
}

const char *net_wifi_status_string(void){
    return esp_at_wifi_status_string();
}

const char *net_wifi_ip_string(void){
    return esp_at_wifi_ip_string();
}

void net_poll(void){
    esp_at_poll();
    esp_device_auth_http_poll();
}

void net_service_pending_socket_closes(struct mobile_user *mobile){
    // Nothing to defer for this backend: socket_impl_close() (esp/net/socket_impl.c)
    // never runs from a re-entrant callback context the way picow's lwIP
    // callbacks can (see net/net_hal.h's contract) - esp_at_poll() only ever
    // runs from the single main loop, synchronously, never nested inside
    // another socket_impl_* call.
    (void)mobile;
}
