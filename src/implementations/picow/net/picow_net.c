// SPDX-License-Identifier: GPL-3.0-only
// cyw43+lwIP implementation of the board-agnostic net_hal.h interface.
#include "net/net_hal.h"
#include "picow_net.h"

#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"

#include "globals.h"
#include "socket_impl.h"
#include "picow_device_auth_http.h"

#define WIFI_CONNECT_MAX_ATTEMPTS 5
#define WIFI_CONNECT_RETRY_DELAY_MS 1000

static int last_connect_errorcode = 0;

bool net_init(void){
    return cyw43_arch_init() == 0;
}

bool net_wifi_connect(const char *ssid, const char *psk, uint32_t timeout_ms){
    last_connect_errorcode = 0;

    if ((strcmp(ssid, WIFI_DEFAULT_SSID) == 0 &&
         strcmp(psk, WIFI_DEFAULT_PASS) == 0) ||
        (ssid[0] == '\0' && psk[0] == '\0')) {

        DEBUG_PRINT_FUNCTION(
            "Wi-Fi credentials are still the defaults; skipping connection attempts."
        );

        return false;
    }

    cyw43_pm_value(CYW43_NO_POWERSAVE_MODE, 200, 1, 1, 10);
    cyw43_arch_enable_sta_mode();

    for (unsigned attempt = 1;
         attempt <= WIFI_CONNECT_MAX_ATTEMPTS;
         attempt++) {

        DEBUG_PRINT_FUNCTION(
            "Starting Wi-Fi connection attempt %u/%u.",
            attempt,
            WIFI_CONNECT_MAX_ATTEMPTS
        );

        /*
         * Start the connection asynchronously.
         *
         * Unlike cyw43_arch_wifi_connect_timeout_ms(), this does not
         * block waiting for the connection to finish.
         */
        int errorcode = cyw43_arch_wifi_connect_async(
            (char *)ssid,
            (char *)psk,
            CYW43_AUTH_WPA2_AES_PSK
        );

        if (errorcode != 0) {
            last_connect_errorcode = errorcode;

            DEBUG_PRINT_FUNCTION(
                "Failed to start Wi-Fi connection. Error: %i",
                errorcode
            );

            return false;
        }

        absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

        while (!time_reached(deadline)) {
            /*
             * Process CYW43/lwIP events.
             */
            cyw43_arch_poll();

            int status = cyw43_tcpip_link_status(
                &cyw43_state,
                CYW43_ITF_STA
            );

            switch (status) {
                case CYW43_LINK_UP:
                    DEBUG_PRINT_FUNCTION(
                        "Device IP: %s",
                        net_wifi_ip_string()
                    );

                    DEBUG_PRINT_FUNCTION(
                        "Connected on attempt %u.",
                        attempt
                    );

                    return true;

                case CYW43_LINK_BADAUTH:
                    /*
                     * Authentication failure is not useful to retry
                     * repeatedly with the same credentials.
                     */
                    last_connect_errorcode = PICO_ERROR_BADAUTH;

                    DEBUG_PRINT_FUNCTION(
                        "Wi-Fi authentication failed."
                    );

                    return false;

                case CYW43_LINK_NONET:
                    /*
                     * The AP was not found.
                     *
                     * Do not wait for cyw43_arch_wifi_connect_timeout_ms()
                     * to decide what to do. We explicitly handle it here.
                     */
                    DEBUG_PRINT_FUNCTION(
                        "Wi-Fi network not found."
                    );

                    goto connection_attempt_failed;

                case CYW43_LINK_FAIL:
                    DEBUG_PRINT_FUNCTION(
                        "Wi-Fi connection failed."
                    );

                    goto connection_attempt_failed;

                case CYW43_LINK_JOIN:
                case CYW43_LINK_NOIP:
                    /*
                     * Still connecting. Keep polling until the deadline.
                     */
                    break;

                case CYW43_LINK_DOWN:
                    /*
                     * Connection has not started / link is down.
                     * Keep polling until timeout.
                     */
                    break;

                default:
                    break;
            }

            /*
             * Avoid spinning at maximum CPU speed.
             * The CYW43 driver is still serviced by cyw43_arch_poll().
             */
            sleep_ms(10);
        }

        /*
         * Our own timeout expired.
         */
        DEBUG_PRINT_FUNCTION(
            "Wi-Fi connection timed out."
        );

connection_attempt_failed:

        if (attempt < WIFI_CONNECT_MAX_ATTEMPTS) {
            /*
             * We gave up on this attempt, but the CYW43439 doesn't know
             * that: it can still be mid-handshake (association/EAPOL) for
             * the join we just abandoned. Starting a new
             * cyw43_arch_wifi_connect_async() on top of that leaves the
             * chip's own join state machine waiting on a handshake that
             * will never resume, which can wedge it so the next join
             * request never gets a response - hanging inside
             * cyw43_arch_poll() with no further retries, timeouts included.
             * Explicitly disassociate first so the retry starts clean.
             */
            cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
            for (int i = 0; i < 10; i++) {
                cyw43_arch_poll();
                sleep_ms(10);
            }

            sleep_ms(WIFI_CONNECT_RETRY_DELAY_MS);
        }
    }

    last_connect_errorcode = PICO_ERROR_TIMEOUT;
    return false;
}

bool net_wifi_last_connect_was_badauth(void){
    return last_connect_errorcode == PICO_ERROR_BADAUTH;
}

void net_wifi_start_ap(const char *ssid, const char *psk){
    cyw43_arch_disable_sta_mode();
    cyw43_arch_enable_ap_mode(
        (char *)ssid,
        (char *)psk,
        CYW43_AUTH_WPA2_AES_PSK
    );
}

const char *net_wifi_status_string(void){
    switch (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA)) {
        case CYW43_LINK_DOWN:
            return "DOWN (wifi not connected)";

        case CYW43_LINK_JOIN:
            return "JOIN (connected, no IP yet)";

        case CYW43_LINK_NOIP:
            return "NOIP (connected, no IP address)";

        case CYW43_LINK_UP:
            return "UP (connected with IP address)";

        case CYW43_LINK_FAIL:
            return "FAIL (connection failed)";

        case CYW43_LINK_NONET:
            return "NONET (SSID not found)";

        case CYW43_LINK_BADAUTH:
            return "BADAUTH (authentication failure)";

        default:
            return "UNKNOWN";
    }
}

const char *net_wifi_ip_string(void){
    return ip4addr_ntoa(netif_ip4_addr(netif_list));
}

void net_poll(void){
    cyw43_arch_poll();
    picow_device_auth_http_poll();
}

void net_service_pending_socket_closes(struct mobile_user *mobile){
    for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
        if (mobile->socket[i]->pending_close) {
            socket_impl_close_commands(mobile->socket[i]);
        }
    }
}