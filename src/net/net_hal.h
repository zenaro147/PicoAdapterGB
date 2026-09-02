#pragma once
#include <stdbool.h>
#include <stdint.h>

struct mobile_user;
struct mobile_addr;

// Board-agnostic network control surface. main.c and the rest of the core
// glue code must only depend on this header (and net/socket_hal.h for
// per-connection socket operations), never on cyw43/lwIP or any other
// backend's types directly. Porting to a different transport (e.g. a plain
// Pico + ESP32 over UART/SPI) only requires a new backend implementing these
// functions, selected at build time via PICOADAPTER_IMPLEMENTATION (see
// src/implementations/picow/net/ for the current cyw43+lwIP backend).

// One-time hardware/stack bring-up. Must be called before any other net_*().
bool net_init(void);

// Attempts to join an existing Wi-Fi network, retrying internally up to a
// backend-defined number of times. Returns false if it never connects
// (caller should then fall back to net_wifi_start_ap()).
bool net_wifi_connect(const char *ssid, const char *psk, uint32_t timeout_ms);

// True if the last net_wifi_connect() failure was due to a rejected
// password, as opposed to a generic/timeout failure (no network found,
// router unreachable, etc.). Only meaningful right after net_wifi_connect()
// returns false.
bool net_wifi_last_connect_was_badauth(void);

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

// Standalone DNS type-A lookup used only for the device-auth side channel's
// own server hostname (see core/adapter_bridge.c) - never seen by libmobile,
// and independent of mobile->socket[]/MOBILE_MAX_CONNECTIONS (which are
// reserved for libmobile's own Mobile Adapter connections and must never be
// borrowed for frontend-only traffic). Queries dns1 then dns2 directly - the
// same servers already configured for the emulated game's own protocol DNS -
// instead of any OS/public resolver, since these hostnames typically only
// resolve on that private network. Starting a new lookup abandons one
// already in flight. Serviced internally by each backend's net_poll(); no
// separate poll call is needed.
void net_device_auth_resolve_start(const char *hostname, const struct mobile_addr *dns1, const struct mobile_addr *dns2);
// True once the lookup started by net_device_auth_resolve_start() has
// finished, successfully or not.
bool net_device_auth_resolve_done(void);
// Valid only once net_device_auth_resolve_done() is true. On success, fills
// ip[4] with the raw resolved IPv4 address and returns true; returns false
// on failure/timeout (both DNS servers unreachable, no A record, malformed
// response, etc).
bool net_device_auth_resolve_result(unsigned char ip[4]);
