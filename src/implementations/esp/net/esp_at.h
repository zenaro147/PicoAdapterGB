#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// ESP-AT v2.3.0.0 (ESP8266) command/response/event engine. This is the only
// file that speaks the AT protocol; net_hal.h/socket_hal.h implementations
// (esp_net.c, net/socket_impl.c) and the web module call into this instead of
// touching esp_uart.h directly. See src/implementations/esp/README.md for the
// exact command set, the CIPRECVMODE=1 (passive receive) choice, and the
// single shared CIPSERVER slot this backend arbitrates between the web UI
// and libmobile's own TCP listen/accept.
//
// Concurrency model:
// - connect/send/recv are async: each call either starts a new ESP-AT command
//   (if the AT "bus" is free) or checks/collects the result of one already in
//   flight, and NEVER blocks. This matches libmobile's own sock_connect/
//   sock_send/sock_recv contracts ("non-blocking, called repeatedly" - see
//   dependences/libmobile/mobile.h), and keeps net_poll()/mobile_loop() from
//   ever stalling on a multi-second TCP handshake or send.
// - Setup/lifecycle calls (wifi join/AP start, server start/stop, socket
//   close) block the caller for a bounded, short timeout. They are not on the
//   Game Boy transfer hot path (mobile_loop(), not the link-cable ISR - see
//   core/adapter_bridge.c's impl_sock_close()), and net_wifi_connect()
//   already blocks main() for seconds per attempt today (see
//   src/implementations/picow/net/picow_net.c for the existing precedent).

enum esp_link_owner {
    ESP_LINK_OWNER_NONE = 0,
    ESP_LINK_OWNER_MOBILE,
    ESP_LINK_OWNER_WEB,
    ESP_LINK_OWNER_DEVICE_AUTH, // see esp_device_auth_dns.c
};

typedef enum {
    ESP_WIFI_JOIN_OK = 0,
    ESP_WIFI_JOIN_TIMEOUT,   // AT+CWJAP error code 1, or our own timeout budget expired
    ESP_WIFI_JOIN_BADAUTH,   // AT+CWJAP error code 2 (wrong password)
    ESP_WIFI_JOIN_NO_AP,     // AT+CWJAP error code 3 (SSID not found)
    ESP_WIFI_JOIN_FAIL,      // AT+CWJAP error code 4, or unrecognized error code
    ESP_WIFI_JOIN_MODULE_ERROR, // the module itself didn't respond/sync
} esp_wifi_join_result_t;

// One-time bring-up: syncs with the module (repeated "AT" probe - there is no
// hardware reset/EN line in this backend's wiring, see README.md), disables
// echo, and puts it into the fixed state this backend assumes for the rest of
// its life (CWMODE, CIPMUX=1, CIPRECVMODE=1, CIPDINFO=0). Returns false if the
// module never answers within the sync budget (see esp_config.h).
bool esp_at_init(void);

// Must be called periodically (see net_poll()): drains UART RX and advances
// the parser/link state machine. Never blocks and never itself issues a
// command. All unsolicited events (WIFI CONNECTED/DISCONNECT, "<id>,CONNECT",
// "<id>,CLOSED", passive-mode "+IPD,<id>,<len>" hints) are only ever
// observed here.
void esp_at_poll(void);

// ---- Wi-Fi ----
// Blocks up to timeout_ms. Internally retries CWJAP association a bounded
// number of times on transient failure, same shape as picow's
// net_wifi_connect() (see net_hal.h).
esp_wifi_join_result_t esp_at_wifi_join(const char *ssid, const char *psk, uint32_t timeout_ms);
bool esp_at_wifi_start_ap(const char *ssid, const char *psk);
bool esp_at_wifi_is_connected(void);
const char *esp_at_wifi_ip_string(void);
const char *esp_at_wifi_status_string(void);

// ---- Links ----
// Claims a free link ID (0..ESP_LINK_COUNT-1) for the given owner, or -1 if
// none is free. Mobile sockets always get the same fixed IDs (see
// esp_config.h's ESP_LINK_MOBILE_BASE); this only matters for the web module.
int esp_at_link_alloc(enum esp_link_owner owner, int link_id_hint);
void esp_at_link_release(int link_id); // marks the slot free again
bool esp_at_link_is_connected(int link_id);
// True exactly once per remote-initiated close (edge-triggered; consuming it
// clears it), so callers can react to "<id>,CLOSED" without polling status.
bool esp_at_link_consume_closed_event(int link_id);
unsigned esp_at_link_rx_pending(int link_id); // bytes currently buffered (CIPRECVMODE=1 hint)

// Non-blocking. Returns 1 connected, 0 in progress (call again), -1 on error.
int esp_at_tcp_connect(int link_id, const char *host, uint16_t port);

// UDP CIPSTART responds immediately (no handshake to wait for), so this is a
// short bounded-blocking call rather than the tri-state above.
bool esp_at_udp_open(int link_id, const char *host, uint16_t port, uint16_t local_port);
// Redirects an already-open UDP link's default remote peer (libmobile may
// retarget per send()/recv() via their addr parameter).
bool esp_at_udp_set_remote(int link_id, const char *host, uint16_t port);

// Non-blocking, same tri-state shape as esp_at_tcp_connect(): returns bytes
// actually sent (confirmed by the module's own "SEND OK") on success, 0 if
// the AT+CIPSEND round-trip (OK, then '>' prompt, then SEND OK) hasn't
// completed yet - call again with the *same* data/size until it does -
// or -1 on error/timeout. Matches mobile.h's own documented sock_send()
// contract. See the comment on this function's definition in esp_at.c for
// the history of why this used to block instead.
int esp_at_send(int link_id, const void *data, unsigned size);

// Non-blocking. Returns bytes read (>=0, 0 if nothing pending), -1 on error.
// A 0 return while a link genuinely has data available means the underlying
// AT+CIPRECVDATA is still in flight (see esp_at.c) - the caller must keep
// polling until it returns nonzero/negative, and `data` must stay valid for
// that whole span, not just for this one call: it's the same "caller keeps
// the buffer valid across retries" requirement esp_at_send() has (see
// socket_impl.c), and there's no local copy of it in between.
int esp_at_recv(int link_id, void *data, unsigned size);

// Bounded-blocking (see file header). Safe to call on an already-closed link.
void esp_at_close(int link_id);

// ---- TCP server (single shared CIPSERVER slot) ----
// Only one of {web, mobile P2P listen} can hold the server at a time - the
// ESP8266 itself only supports one CIPSERVER (ESP-AT TCP/IP AT command doc:
// "Only one server can be created at most"). Bounded-blocking.
bool esp_at_server_start(uint16_t port, enum esp_link_owner owner);
void esp_at_server_stop(enum esp_link_owner owner); // no-op if owner doesn't hold it
enum esp_link_owner esp_at_server_owner(void);
// Claims the next unclaimed incoming connection accepted while the server is
// owned by <owner>, if any. Returns its link ID, or -1 if none is pending.
int esp_at_server_accept(enum esp_link_owner owner);
