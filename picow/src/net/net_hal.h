#pragma once
#include <stdbool.h>
#include <stdint.h>

struct mobile_user;

// Board-agnostic network control surface. main.c and the rest of the adapter
// glue code must only depend on this header, never on cyw43/lwIP directly.
// Porting to a different transport (e.g. a plain Pico + ESP32 over UART/SPI)
// only requires a new backend implementing these functions, selected at
// build time (see net/picow/ for the current cyw43+lwIP backend).

// One-time hardware/stack bring-up. Must be called before any other net_*().
bool net_init(void);

// Attempts to join an existing Wi-Fi network, retrying internally up to a
// backend-defined number of times. Returns false if it never connects
// (caller should then fall back to net_wifi_start_ap()).
bool net_wifi_connect(const char *ssid, const char *psk, uint32_t timeout_ms);

// Starts an access point with the given credentials, for first-time setup.
void net_wifi_start_ap(const char *ssid, const char *psk);

// Human-readable current link status, for logging only.
const char *net_wifi_status_string(void);

// Current IPv4 address as a string (station or AP mode), for logging only.
const char *net_wifi_ip_string(void);

// Must be called periodically from the main loop to service the network
// stack (accept/recv/send callbacks, DHCP, etc.).
void net_poll(void);

// Finalizes any per-connection socket teardown that had to be deferred while
// a libmobile callback was executing (see socket_impl's pending_close).
void net_service_pending_socket_closes(struct mobile_user *mobile);
