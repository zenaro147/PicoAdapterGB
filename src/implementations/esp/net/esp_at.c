#include "esp_at.h"
#include "esp_config.h"
#include "esp_uart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "pico/time.h"
#include "globals.h"

// ---------------------------------------------------------------------
// Link table
// ---------------------------------------------------------------------

struct esp_link {
    bool in_use;
    enum esp_link_owner owner;
    bool connected;
    bool remote_closed;   // edge flag, consumed by esp_at_link_consume_closed_event()
    bool server_pending;  // unclaimed incoming connection accepted by the shared CIPSERVER
    unsigned rx_pending;  // bytes buffered on the module (CIPRECVMODE=1 hint, from +IPD events)
};

static struct esp_link links[ESP_LINK_COUNT];

static enum esp_link_owner server_owner = ESP_LINK_OWNER_NONE;
static uint16_t server_port = 0;

// ---------------------------------------------------------------------
// Wi-Fi cached state (updated only from unsolicited events, see process_line())
// ---------------------------------------------------------------------

static volatile bool wifi_associated = false;
static volatile bool wifi_has_ip = false;
static char wifi_ip_cache[16] = "0.0.0.0";

// ---------------------------------------------------------------------
// AT command engine: exactly one command in flight at a time, matching the
// module's own serial command model. Every issuing function either starts a
// new command (if free) or, if one is already active for a *different*
// purpose, leaves the wire alone and lets the caller retry later - callers on
// the hot path (connect/send/recv) are written to tolerate that by contract
// (see esp_at.h).
// ---------------------------------------------------------------------

enum at_cmd_kind {
    AT_CMD_NONE = 0,
    AT_CMD_PLAIN,       // wait for a bare OK/ERROR line
    AT_CMD_CIPSTART,    // "CONNECT"/"<id>,CONNECT" then OK, or ERROR
    AT_CMD_CIPSEND,     // OK, then '>' prompt (we write the payload), then SEND OK/SEND FAIL/ERROR/busy
    AT_CMD_CIPRECVDATA, // "+CIPRECVDATA:<len>," then <len> raw bytes, then OK
    AT_CMD_CWJAP,       // WIFI CONNECTED / WIFI GOT IP / OK, or +CWJAP:<code> then ERROR
};

static volatile bool at_busy = false;
static enum at_cmd_kind at_kind = AT_CMD_NONE;
static int at_link = -1;
static uint64_t at_started_us = 0;
static uint32_t at_timeout_ms = 0;
static volatile bool at_done = false;
static bool at_success = false;
static int at_result_code = -1; // e.g. CWJAP error code, or CIPRECVDATA/CIPSEND byte count

// CIPSTART sub-state
static bool at_connect_seen = false;

// CIPSEND sub-state: payload ownership stays with the caller (see
// esp_at_send()/socket_impl_send() - libmobile keeps the buffer valid across
// retries of the same unsent chunk), we only ever read from it.
static const uint8_t *at_tx_payload = NULL;
static unsigned at_tx_payload_len = 0;
static bool at_tx_cmd_ok = false;   // first "OK" (command accepted) seen
static bool at_tx_written = false;  // payload bytes already written to UART

// CIPRECVDATA sub-state (binary-safe capture, bypasses line buffering)
enum rxp_state { RXP_LINE = 0, RXP_LEN, RXP_PAYLOAD };
static enum rxp_state rxp = RXP_LINE;
static unsigned rxp_len_value = 0;
static uint8_t *at_rx_capture_buf = NULL;
static unsigned at_rx_capture_cap = 0;
static unsigned at_rx_capture_len = 0;
static unsigned at_rx_capture_got = 0;

// Line assembly buffer (plain ASCII protocol lines only - CIPRECVDATA's
// binary payload is diverted to at_rx_capture_buf via rxp, never stored here).
#define AT_LINE_MAX 128
static char line_buf[AT_LINE_MAX];
static unsigned line_len = 0;

static void at_finish(bool success, int result_code){
    at_success = success;
    at_result_code = result_code;
    at_done = true;
}

static void at_begin(enum at_cmd_kind kind, int link_id, uint32_t timeout_ms){
    at_kind = kind;
    at_link = link_id;
    at_started_us = time_us_64();
    at_timeout_ms = timeout_ms;
    at_done = false;
    at_success = false;
    at_result_code = -1;
    at_connect_seen = false;
    at_tx_cmd_ok = false;
    at_tx_written = false;
    rxp = RXP_LINE;
    rxp_len_value = 0;
    at_rx_capture_got = 0;
    line_len = 0;
    at_busy = true;
}

static void at_write_line(const char *fmt, ...){
    char buf[144];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len < 0) return;
    if ((size_t)len >= sizeof(buf)) len = sizeof(buf) - 1;
    esp_uart_write((const uint8_t *)buf, (size_t)len);
    esp_uart_write((const uint8_t *)"\r\n", 2);
}

// ---------------------------------------------------------------------
// Line/event parsing
// ---------------------------------------------------------------------

static void mark_wifi_down(void){
    wifi_associated = false;
    wifi_has_ip = false;
    for (int i = 0; i < ESP_LINK_COUNT; i++){
        if (links[i].in_use){
            links[i].connected = false;
            links[i].remote_closed = true;
        }
    }
}

// Parses a leading "<digit>," prefix (a link ID, 0..ESP_LINK_COUNT-1) off of
// an unsolicited event line. Returns the link id, or -1 if the line doesn't
// start with that shape.
static int parse_link_prefix(const char *line, const char **rest){
    if (line[0] < '0' || line[0] > '9') return -1;
    if (line[1] != ',') return -1;
    int id = line[0] - '0';
    if (id >= ESP_LINK_COUNT) return -1;
    *rest = line + 2;
    return id;
}

static void handle_connect_event(int id){
    if (id < 0) return;
    if (!links[id].in_use && server_owner != ESP_LINK_OWNER_NONE) {
        // New inbound connection accepted by the shared CIPSERVER slot.
        links[id].in_use = true;
        links[id].owner = server_owner;
        links[id].server_pending = true;
        links[id].remote_closed = false;
        links[id].rx_pending = 0;
    }
    links[id].connected = true;
}

static void handle_closed_event(int id){
    if (id < 0) return;
    links[id].connected = false;
    if (links[id].in_use) links[id].remote_closed = true;
}

static void handle_ipd_event(const char *rest){
    // rest points right after "<id>," was already stripped by the caller for
    // the outer digit, but +IPD carries its own "<id>,<len>" pair after the
    // "+IPD," keyword, so this parses that pair directly.
    int id = 0;
    if (*rest < '0' || *rest > '9') return;
    id = *rest - '0';
    rest++;
    if (*rest != ',' || id >= ESP_LINK_COUNT) return;
    rest++;
    unsigned len = 0;
    bool any = false;
    while (*rest >= '0' && *rest <= '9') { len = len * 10 + (unsigned)(*rest - '0'); rest++; any = true; }
    if (!any) return;
    links[id].rx_pending += len;
}

static bool line_is(const char *line, const char *literal){
    return strcmp(line, literal) == 0;
}

static bool line_starts_with(const char *line, const char *prefix){
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

static void process_line(const char *line){
    if (line[0] == '\0') return; // blank separator lines are routine, not events

    // ---- Unsolicited events: always processed, regardless of what command
    // (if any) is currently active. ----
    if (line_is(line, "WIFI CONNECTED")) {
        wifi_associated = true;
        return;
    }
    if (line_is(line, "WIFI GOT IP")) {
        wifi_has_ip = true;
        return;
    }
    if (line_starts_with(line, "WIFI DISCONNECT")) {
        mark_wifi_down();
        return;
    }
    if (line_starts_with(line, "+IPD,")) {
        handle_ipd_event(line + strlen("+IPD,"));
        return;
    }
    {
        const char *rest;
        int id = parse_link_prefix(line, &rest);
        if (id >= 0 && strcmp(rest, "CONNECT") == 0) { handle_connect_event(id); return; }
        if (id >= 0 && strcmp(rest, "CLOSED") == 0) { handle_closed_event(id); return; }
    }

    // ---- Active-command completion. Everything below only matters if a
    // command is currently in flight. ----
    if (!at_busy) return;

    switch (at_kind){
    case AT_CMD_PLAIN:
        if (line_is(line, "OK")) at_finish(true, 0);
        else if (line_is(line, "ERROR")) at_finish(false, -1);
        break;

    case AT_CMD_CIPSTART:
        if (strcmp(line, "CONNECT") == 0) { at_connect_seen = true; break; }
        {
            const char *rest;
            int id = parse_link_prefix(line, &rest);
            if (id == at_link && strcmp(rest, "CONNECT") == 0) { at_connect_seen = true; break; }
        }
        if (line_is(line, "ALREADY CONNECTED")) { at_connect_seen = true; break; }
        if (line_is(line, "OK")) { at_finish(at_connect_seen, 0); break; }
        if (line_is(line, "ERROR")) { at_finish(false, -1); break; }
        break;

    case AT_CMD_CWJAP:
        if (line_starts_with(line, "+CWJAP:")) {
            at_result_code = atoi(line + strlen("+CWJAP:"));
            break;
        }
        if (line_is(line, "OK")) { at_finish(true, 0); break; }
        if (line_is(line, "ERROR")) { at_finish(false, at_result_code); break; }
        if (line_is(line, "FAIL")) { at_finish(false, at_result_code); break; }
        break;

    case AT_CMD_CIPSEND:
        if (!at_tx_cmd_ok && line_is(line, "OK")) { at_tx_cmd_ok = true; break; }
        if (line_is(line, "SEND OK")) { at_finish(true, (int)at_tx_payload_len); break; }
        if (line_is(line, "SEND FAIL")) { at_finish(false, -1); break; }
        if (line_is(line, "ERROR")) { at_finish(false, -1); break; }
        if (line_starts_with(line, "busy")) { at_finish(false, -1); break; }
        break;

    case AT_CMD_CIPRECVDATA:
        // Reached only for the trailing "OK" after the binary payload has
        // already been fully captured by the byte-level state machine below
        // (rxp == RXP_LINE again by then).
        if (line_is(line, "OK")) { at_finish(true, (int)at_rx_capture_len); break; }
        if (line_is(line, "ERROR")) { at_finish(false, -1); break; }
        break;

    default:
        break;
    }
}

// Byte-level feed: routes bytes either into the plain-text line assembler or,
// while capturing a CIPRECVDATA response, into the binary-safe path that
// never treats payload bytes as line-framing characters (see esp_at.h's
// header comment on why passive receive makes this tractable).
static void feed_byte(uint8_t b){
    if (rxp == RXP_PAYLOAD) {
        if (at_rx_capture_got < at_rx_capture_cap) {
            at_rx_capture_buf[at_rx_capture_got] = b;
        }
        at_rx_capture_got++;
        if (at_rx_capture_got >= at_rx_capture_len) rxp = RXP_LINE;
        return;
    }

    if (rxp == RXP_LEN) {
        if (b >= '0' && b <= '9') {
            rxp_len_value = rxp_len_value * 10 + (unsigned)(b - '0');
            return;
        }
        if (b == ',') {
            at_rx_capture_len = rxp_len_value;
            at_rx_capture_got = 0;
            rxp = (at_rx_capture_len > 0) ? RXP_PAYLOAD : RXP_LINE;
            return;
        }
        // Unexpected byte in the middle of the length field: bail out of the
        // specialized capture and let normal line parsing resynchronize on
        // the next \r/\n instead of getting stuck here forever.
        rxp = RXP_LINE;
        line_len = 0;
        return;
    }

    // RXP_LINE (default): plain ASCII protocol text.
    if (at_busy && at_kind == AT_CMD_CIPSEND && !at_tx_written && at_tx_cmd_ok
            && line_len == 0 && b == '>') {
        // The module's send-data prompt has no line terminator of its own.
        esp_uart_write(at_tx_payload, at_tx_payload_len);
        at_tx_written = true;
        return;
    }

    if (b == '\r') return; // dropped; '\n' alone ends a line
    if (b == '\n') {
        line_buf[line_len < AT_LINE_MAX ? line_len : AT_LINE_MAX - 1] = '\0';
        process_line(line_buf);
        line_len = 0;
        return;
    }

    if (line_len < AT_LINE_MAX - 1) line_buf[line_len++] = (char)b;
    else line_len++; // keep counting so an oversized line is still fully skipped

    // Binary-safe hand-off: the header of a CIPRECVDATA response ("+CIPRECVDATA:")
    // has no line terminator before the raw payload begins, so it can't wait
    // for '\n' like every other response. Detected the moment the line buffer
    // exactly matches the literal prefix.
    if (at_busy && at_kind == AT_CMD_CIPRECVDATA && line_len == 13 &&
            memcmp(line_buf, "+CIPRECVDATA:", 13) == 0) {
        rxp = RXP_LEN;
        rxp_len_value = 0;
        line_len = 0;
    }
}

// ---------------------------------------------------------------------
// Public: init / poll
// ---------------------------------------------------------------------

static bool at_run_blocking(enum at_cmd_kind kind, int link_id, uint32_t timeout_ms,
        const char *fmt, ...){
    if (at_busy) return false; // caller must not overlap a blocking call with an active async one

    at_begin(kind, link_id, timeout_ms);

    char buf[144];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len < 0) len = 0;
    if ((size_t)len >= sizeof(buf)) len = sizeof(buf) - 1;
    esp_uart_write((const uint8_t *)buf, (size_t)len);
    esp_uart_write((const uint8_t *)"\r\n", 2);

    while (!at_done) {
        esp_at_poll();
        if (!at_busy) break; // esp_at_poll() finalizes timeouts itself
        sleep_ms(5);
    }
    at_busy = false;
    return at_success;
}

static bool at_sync_once(void){
    line_len = 0;
    at_kind = AT_CMD_PLAIN;
    at_done = false;
    at_success = false;
    at_busy = true;
    at_started_us = time_us_64();
    at_timeout_ms = ESP_AT_TIMEOUT_BASIC_MS;
    esp_uart_write((const uint8_t *)"AT\r\n", 4);
    while (!at_done) {
        esp_at_poll();
        if (!at_busy) break;
        sleep_ms(5);
    }
    at_busy = false;
    return at_success;
}

bool esp_at_init(void){
    esp_uart_init();
    memset(links, 0, sizeof(links));
    server_owner = ESP_LINK_OWNER_NONE;
    wifi_associated = false;
    wifi_has_ip = false;

    bool synced = false;
    for (int attempt = 0; attempt < ESP_AT_SYNC_ATTEMPTS; attempt++) {
        DEBUG_PRINT_FUNCTION("[ESP-AT] Sync attempt %d/%d...", attempt + 1, ESP_AT_SYNC_ATTEMPTS);
        if (at_sync_once()) { synced = true; break; }
        sleep_ms(ESP_AT_SYNC_ATTEMPT_DELAY_MS);
    }
    if (!synced) {
        DEBUG_PRINT_FUNCTION("[ESP-AT] Module did not respond to AT probe.");
        return false;
    }
    DEBUG_PRINT_FUNCTION("[ESP-AT] Module responding.");

    if (!at_run_blocking(AT_CMD_PLAIN, -1, ESP_AT_TIMEOUT_BASIC_MS, "ATE0")) {
        DEBUG_PRINT_FUNCTION("[ESP-AT] Failed to disable echo.");
        return false;
    }
    // Station+AP mode: station for normal operation, AP available for the
    // first-time-setup hotspot fallback (see esp_at_wifi_start_ap()).
    if (!at_run_blocking(AT_CMD_PLAIN, -1, ESP_AT_TIMEOUT_BASIC_MS, "AT+CWMODE=3")) {
        DEBUG_PRINT_FUNCTION("[ESP-AT] Failed to set CWMODE.");
        return false;
    }
    if (!at_run_blocking(AT_CMD_PLAIN, -1, ESP_AT_TIMEOUT_BASIC_MS, "AT+CIPMUX=1")) {
        DEBUG_PRINT_FUNCTION("[ESP-AT] Failed to enable CIPMUX.");
        return false;
    }
    if (!at_run_blocking(AT_CMD_PLAIN, -1, ESP_AT_TIMEOUT_BASIC_MS, "AT+CIPRECVMODE=1")) {
        DEBUG_PRINT_FUNCTION("[ESP-AT] Failed to enable passive receive mode.");
        return false;
    }
    if (!at_run_blocking(AT_CMD_PLAIN, -1, ESP_AT_TIMEOUT_BASIC_MS, "AT+CIPDINFO=0")) {
        DEBUG_PRINT_FUNCTION("[ESP-AT] Failed to configure CIPDINFO.");
        return false;
    }

    DEBUG_PRINT_FUNCTION("[ESP-AT] Ready.");
    return true;
}

void esp_at_poll(void){
    uint8_t b;
    while (esp_uart_read_byte(&b)) feed_byte(b);

    if (at_busy && !at_done) {
        uint64_t elapsed_ms = (time_us_64() - at_started_us) / 1000;
        if (elapsed_ms >= at_timeout_ms) {
            at_finish(false, -1);
        }
    }
    // Deliberately does NOT clear at_busy on at_done here: the owning caller
    // (esp_at_tcp_connect()/esp_at_send()/esp_at_recv() for the async
    // commands, at_run_blocking()/at_sync_once() for the blocking ones) is
    // responsible for consuming the result and clearing at_busy itself. If
    // this cleared at_busy automatically, an unrelated caller could issue a
    // brand new command on the freed-up "bus" before the original one ever
    // reads its result, silently discarding it (see esp_at_close()'s forced
    // reclaim for the one legitimate way a command is abandoned unread, when
    // libmobile cancels a connect/send by closing the socket instead).
}

// ---------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------

esp_wifi_join_result_t esp_at_wifi_join(const char *ssid, const char *psk, uint32_t timeout_ms){
    if (at_busy) return ESP_WIFI_JOIN_MODULE_ERROR;

    bool ok = at_run_blocking(AT_CMD_CWJAP, -1, timeout_ms,
        "AT+CWJAP=\"%s\",\"%s\"", ssid, psk);
    if (ok) return ESP_WIFI_JOIN_OK;

    switch (at_result_code){
        case 1: return ESP_WIFI_JOIN_TIMEOUT;
        case 2: return ESP_WIFI_JOIN_BADAUTH;
        case 3: return ESP_WIFI_JOIN_NO_AP;
        case 4: return ESP_WIFI_JOIN_FAIL;
        default: return ESP_WIFI_JOIN_FAIL;
    }
}

bool esp_at_wifi_start_ap(const char *ssid, const char *psk){
    if (at_busy) return false;
    // ecn=3 (WPA2_PSK), channel 1, up to 4 stations, broadcast SSID.
    return at_run_blocking(AT_CMD_PLAIN, -1, ESP_AT_TIMEOUT_BASIC_MS,
        "AT+CWSAP=\"%s\",\"%s\",1,3,4,0", ssid, psk);
}

bool esp_at_wifi_is_connected(void){
    return wifi_associated && wifi_has_ip;
}

const char *esp_at_wifi_ip_string(void){
    if (!wifi_has_ip) { strcpy(wifi_ip_cache, "0.0.0.0"); return wifi_ip_cache; }
    if (at_busy) return wifi_ip_cache; // stale but non-blocking; refreshed next call

    // AT+CIFSR prints one line per interface, e.g. +CIFSR:STAIP,"192.168.1.5".
    // Captured by temporarily repurposing the plain-line path: we scan for
    // the STAIP line ourselves instead of adding a whole new at_kind, since
    // this is only ever called for logging (see net_hal.h).
    at_begin(AT_CMD_PLAIN, -1, ESP_AT_TIMEOUT_BASIC_MS);
    esp_uart_write((const uint8_t *)"AT+CIFSR\r\n", 10);
    char found_ip[16] = "";
    while (!at_done) {
        uint8_t b;
        while (esp_uart_read_byte(&b)) {
            if (b == '\r') continue;
            if (b == '\n') {
                line_buf[line_len < AT_LINE_MAX ? line_len : AT_LINE_MAX - 1] = '\0';
                if (line_starts_with(line_buf, "+CIFSR:STAIP,\"")) {
                    const char *start = line_buf + strlen("+CIFSR:STAIP,\"");
                    const char *end = strchr(start, '"');
                    if (end && (size_t)(end - start) < sizeof(found_ip)) {
                        memcpy(found_ip, start, (size_t)(end - start));
                        found_ip[end - start] = '\0';
                    }
                    line_len = 0;
                    continue;
                }
                process_line(line_buf);
                line_len = 0;
                continue;
            }
            if (line_len < AT_LINE_MAX - 1) line_buf[line_len++] = (char)b;
            else line_len++;
        }
        uint64_t elapsed_ms = (time_us_64() - at_started_us) / 1000;
        if (elapsed_ms >= at_timeout_ms) at_finish(false, -1);
        if (!at_done) sleep_ms(2);
    }
    at_busy = false;
    if (found_ip[0]) strncpy(wifi_ip_cache, found_ip, sizeof(wifi_ip_cache) - 1);
    return wifi_ip_cache;
}

const char *esp_at_wifi_status_string(void){
    if (wifi_associated && wifi_has_ip) return "UP (connected with IP address)";
    if (wifi_associated) return "NOIP (connected, no IP address yet)";
    return "DOWN (wifi not connected)";
}

// ---------------------------------------------------------------------
// Links
// ---------------------------------------------------------------------

int esp_at_link_alloc(enum esp_link_owner owner, int link_id_hint){
    if (link_id_hint >= 0 && link_id_hint < ESP_LINK_COUNT && !links[link_id_hint].in_use) {
        links[link_id_hint].in_use = true;
        links[link_id_hint].owner = owner;
        links[link_id_hint].connected = false;
        links[link_id_hint].remote_closed = false;
        links[link_id_hint].server_pending = false;
        links[link_id_hint].rx_pending = 0;
        return link_id_hint;
    }
    for (int i = 0; i < ESP_LINK_COUNT; i++){
        if (!links[i].in_use){
            links[i].in_use = true;
            links[i].owner = owner;
            links[i].connected = false;
            links[i].remote_closed = false;
            links[i].server_pending = false;
            links[i].rx_pending = 0;
            return i;
        }
    }
    return -1;
}

void esp_at_link_release(int link_id){
    if (link_id < 0 || link_id >= ESP_LINK_COUNT) return;
    memset(&links[link_id], 0, sizeof(links[link_id]));
}

bool esp_at_link_is_connected(int link_id){
    if (link_id < 0 || link_id >= ESP_LINK_COUNT) return false;
    return links[link_id].connected;
}

bool esp_at_link_consume_closed_event(int link_id){
    if (link_id < 0 || link_id >= ESP_LINK_COUNT) return false;
    bool v = links[link_id].remote_closed;
    links[link_id].remote_closed = false;
    return v;
}

unsigned esp_at_link_rx_pending(int link_id){
    if (link_id < 0 || link_id >= ESP_LINK_COUNT) return 0;
    return links[link_id].rx_pending;
}

int esp_at_tcp_connect(int link_id, const char *host, uint16_t port){
    if (link_id < 0 || link_id >= ESP_LINK_COUNT) return -1;

    if (at_busy) {
        if (at_kind == AT_CMD_CIPSTART && at_link == link_id) {
            if (!at_done) return 0; // still in progress
            at_busy = false;
            return at_success ? 1 : -1;
        }
        return 0; // bus busy with something else; caller retries
    }

    at_begin(AT_CMD_CIPSTART, link_id, ESP_AT_TIMEOUT_SOCKET_MS);
    at_write_line("AT+CIPSTART=%d,\"TCP\",\"%s\",%u", link_id, host, (unsigned)port);
    return 0;
}

bool esp_at_udp_open(int link_id, const char *host, uint16_t port, uint16_t local_port){
    if (link_id < 0 || link_id >= ESP_LINK_COUNT) return false;
    if (at_busy) return false;
    bool ok;
    if (local_port != 0) {
        ok = at_run_blocking(AT_CMD_CIPSTART, link_id, ESP_AT_TIMEOUT_SOCKET_MS,
            "AT+CIPSTART=%d,\"UDP\",\"%s\",%u,%u,0", link_id, host, (unsigned)port, (unsigned)local_port);
    } else {
        ok = at_run_blocking(AT_CMD_CIPSTART, link_id, ESP_AT_TIMEOUT_SOCKET_MS,
            "AT+CIPSTART=%d,\"UDP\",\"%s\",%u", link_id, host, (unsigned)port);
    }
    if (ok) links[link_id].connected = true;
    return ok;
}

bool esp_at_udp_set_remote(int link_id, const char *host, uint16_t port){
    // ESP-AT has no dedicated "retarget" command; re-issuing CIPSTART on an
    // already-open UDP link updates its default remote peer.
    return esp_at_udp_open(link_id, host, port, 0);
}

int esp_at_send(int link_id, const void *data, unsigned size){
    if (link_id < 0 || link_id >= ESP_LINK_COUNT || size == 0) return -1;

    if (at_busy) {
        if (at_kind == AT_CMD_CIPSEND && at_link == link_id) {
            if (!at_done) return 0; // still in progress
            at_busy = false;
            return at_success ? at_result_code : -1;
        }
        return 0; // bus busy with something else; caller retries
    }

    at_begin(AT_CMD_CIPSEND, link_id, ESP_AT_TIMEOUT_SEND_MS);
    at_tx_payload = (const uint8_t *)data;
    at_tx_payload_len = size;
    at_write_line("AT+CIPSEND=%d,%u", link_id, size);
    return 0;
}

int esp_at_recv(int link_id, void *data, unsigned size){
    if (link_id < 0 || link_id >= ESP_LINK_COUNT) return -1;

    if (at_busy) {
        if (at_kind == AT_CMD_CIPRECVDATA && at_link == link_id) {
            if (!at_done) return 0; // still in progress
            at_busy = false;
            if (!at_success) return -1;
            unsigned got = at_rx_capture_got < at_rx_capture_cap ? at_rx_capture_got : at_rx_capture_cap;
            if (links[link_id].rx_pending >= got) links[link_id].rx_pending -= got;
            else links[link_id].rx_pending = 0;
            return (int)got;
        }
        return 0; // bus busy with something else; caller retries
    }

    if (links[link_id].rx_pending == 0) return 0;
    unsigned want = links[link_id].rx_pending < size ? links[link_id].rx_pending : size;
    if (want == 0) return 0;

    at_begin(AT_CMD_CIPRECVDATA, link_id, ESP_AT_TIMEOUT_BASIC_MS);
    at_rx_capture_buf = (uint8_t *)data;
    at_rx_capture_cap = size;
    at_write_line("AT+CIPRECVDATA=%d,%u", link_id, want);
    return 0;
}

void esp_at_close(int link_id){
    if (link_id < 0 || link_id >= ESP_LINK_COUNT) return;

    // libmobile is allowed to cancel an in-progress connect/send by closing
    // the socket instead of ever collecting its result (see mobile.h's
    // sock_connect/sock_send docs). Reclaim the AT "bus" here so it doesn't
    // stay wedged forever waiting for a caller that will never come back -
    // whatever response eventually arrives for the abandoned command is
    // simply ignored (process_line() only acts on it while at_busy is true).
    // Sending CIPCLOSE while that response hasn't actually arrived on the
    // wire yet is a narrow race this backend can't fully rule out without
    // hardware in hand - see README.md's known-limitations section.
    if (at_busy && at_link == link_id) at_busy = false;

    if (!links[link_id].connected) { esp_at_link_release(link_id); return; }
    if (!at_busy) at_run_blocking(AT_CMD_PLAIN, link_id, ESP_AT_TIMEOUT_CLOSE_MS, "AT+CIPCLOSE=%d", link_id);
    esp_at_link_release(link_id);
}

// ---------------------------------------------------------------------
// Shared TCP server slot
// ---------------------------------------------------------------------

bool esp_at_server_start(uint16_t port, enum esp_link_owner owner){
    if (server_owner != ESP_LINK_OWNER_NONE) return server_owner == owner;
    if (at_busy) return false;
    if (!at_run_blocking(AT_CMD_PLAIN, -1, ESP_AT_TIMEOUT_BASIC_MS, "AT+CIPSERVERMAXCONN=2")) {
        // Not fatal: proceed with the module's own default backlog.
    }
    bool ok = at_run_blocking(AT_CMD_PLAIN, -1, ESP_AT_TIMEOUT_BASIC_MS,
        "AT+CIPSERVER=1,%u", (unsigned)port);
    if (ok) { server_owner = owner; server_port = port; }
    return ok;
}

void esp_at_server_stop(enum esp_link_owner owner){
    if (server_owner != owner) return;
    if (!at_busy) at_run_blocking(AT_CMD_PLAIN, -1, ESP_AT_TIMEOUT_BASIC_MS, "AT+CIPSERVER=0");
    // Release every link this owner still holds that came from the server,
    // so a stale connection can't be mistaken for a fresh one later.
    for (int i = 0; i < ESP_LINK_COUNT; i++){
        if (links[i].in_use && links[i].owner == owner) esp_at_link_release(i);
    }
    server_owner = ESP_LINK_OWNER_NONE;
    server_port = 0;
}

enum esp_link_owner esp_at_server_owner(void){
    return server_owner;
}

int esp_at_server_accept(enum esp_link_owner owner){
    if (server_owner != owner) return -1;
    for (int i = 0; i < ESP_LINK_COUNT; i++){
        if (links[i].in_use && links[i].owner == owner && links[i].server_pending && links[i].connected){
            links[i].server_pending = false;
            return i;
        }
    }
    return -1;
}
