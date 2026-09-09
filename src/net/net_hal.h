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

// One-shot outbound HTTP GET for the device-auth side channel (see
// core/adapter_bridge.c) - independent of mobile->socket[]/
// MOBILE_MAX_CONNECTIONS (reserved for libmobile's own Mobile Adapter
// connections, never to be borrowed for frontend-only traffic). libmobile
// itself now resolves the server's address before this call ever happens
// (through the session's own configured DNS1/DNS2), so this backend only
// ever needs to open a plain TCP connection to an already-known IP - no DNS
// step of our own. `request_line` is the full first line already built by
// the caller (e.g.
// "GET /api/...&sig=... HTTP/1.0"), sent together with a Host header built
// from `ip` and `Connection: close`, then the response is read only far
// enough to parse the status line - the body (if any) is discarded.
// Starting a new request abandons one already in flight.
void net_device_auth_http_get_start(const unsigned char ip[4], uint16_t port, const char *request_line);
// True once the request started by net_device_auth_http_get_start() has
// finished, successfully or not.
bool net_device_auth_http_get_done(void);
// Valid only once net_device_auth_http_get_done() is true. Returns the
// parsed HTTP status code (e.g. 200, 400, 403) on success, or -1 on
// transport failure/timeout/malformed response.
int net_device_auth_http_get_result(void);
// Valid only once net_device_auth_http_get_done() is true. Returns the
// response body exactly as received, with *len set to its length; the
// pointer stays valid until the next net_device_auth_http_get_start().
// Used by the device-auth counter query, whose answer is "<counter> <sig>".
// Hand it to libmobile untouched: it verifies the signature and parses
// strictly itself, so a backend must not validate, trim or reinterpret it.
// The body is captured up to a fixed ceiling sized for that answer, so a
// longer one arrives truncated - which libmobile then rejects, as it should.
const char *net_device_auth_http_get_body(unsigned *len);
