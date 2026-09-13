// SPDX-License-Identifier: GPL-3.0-only
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

// Escapes ',', '"' and '\' with a leading '\', as required by ESP-AT for
// quoted string arguments (AT+CWJAP/AT+CWSAP SSID and password - see the
// ESP-AT Wi-Fi AT command doc). Without this, any of those characters in a
// password breaks the module's own command parsing - not a connectivity
// issue, but indistinguishable from one without a packet capture. dstsize
// must be at least 2*strlen(src)+1.
static void at_escape_quoted(char *dst, size_t dstsize, const char *src){
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 2 < dstsize; i++){
        char c = src[i];
        if (c == ',' || c == '"' || c == '\\') dst[j++] = '\\';
        dst[j++] = c;
    }
    dst[j] = '\0';
}

static void at_write_line(const char *fmt, ...){
    // Sized for the worst case AT+CWJAP="<escaped ssid>","<escaped psk>":
    // SSID_LENGHT/PASS_LENGHT (globals.h) each fully escaped (2x) plus the
    // command/quote/comma overhead.
    char buf[256];
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

    // In CIPMUX=1 mode (always on for this backend - see esp_at_init()), an
    // outbound AT+CIPSTART's "OK" only means the command was accepted; the
    // actual TCP handshake completing is signaled by this same unsolicited
    // "<id>,CONNECT" event, arriving separately (often after "OK", not
    // before). This line is consumed here, before process_line() would ever
    // reach the AT_CMD_CIPSTART case in its switch below for it, so an
    // in-flight connect for this link must be finished from here directly -
    // that switch case only ever sees "OK"/"ERROR" for this command kind.
    if (at_busy && !at_done && at_kind == AT_CMD_CIPSTART && at_link == id) {
        at_finish(true, 0);
    }
}

static void handle_closed_event(int id){
    if (id < 0) return;
    links[id].connected = false;
    if (links[id].in_use) {
        links[id].remote_closed = true;
        // Cosmetic parity with picow's tcp_err callback ("TCP Generic
        // Error") - the remote end closed this connection on its own
        // (unlike esp_at_close()'s own "Socket Closed." print, which is for
        // a close we initiated ourselves).
        DEBUG_PRINT_FUNCTION("Remote closed connection (link %d).", id);
    }
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
        // Real success is the unsolicited "<id>,CONNECT" event, handled (and
        // this command finished) directly from handle_connect_event() -
        // that line never reaches this switch, since it's consumed by
        // process_line()'s unsolicited-event checks above before at_busy is
        // even looked at. A bare "CONNECT" (no link-ID prefix) only happens
        // in CIPMUX=0 mode, which this backend never uses, but is still
        // handled here defensively.
        if (strcmp(line, "CONNECT") == 0) { at_connect_seen = true; break; }
        if (line_is(line, "ALREADY CONNECTED")) { at_connect_seen = true; break; }
        if (line_is(line, "OK")) {
            // In CIPMUX=1 mode, "OK" only means the command was accepted -
            // it commonly arrives before the real "<id>,CONNECT" completion
            // event, sometimes several hundred ms before the TCP handshake
            // actually finishes. Finishing here unconditionally used to
            // report every connect as failed (at_connect_seen was always
            // still false at this point) even though the module went on to
            // connect successfully moments later. Only finish here if a bare
            // CONNECT/ALREADY CONNECTED already arrived first; otherwise
            // keep waiting for handle_connect_event() or a timeout/ERROR.
            if (at_connect_seen) at_finish(true, 0);
            break;
        }
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
        if (line_is(line, "ERROR")) {
            // A plain ERROR here (instead of the module just returning
            // "0 bytes, OK") commonly means the remote closed the
            // connection right around when this was requested - handled as
            // the standard "remote closed" signal, not a hard failure, by
            // socket_impl_recv() (see its comment on the rc < 0 case).
            at_finish(false, -1);
            break;
        }
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

    if (esp_uart_take_overflow()) {
        // The RX ring buffer dropped a byte somewhere in what was just fed
        // to feed_byte() above (see esp_uart.h). Whatever line/length/
        // payload framing state that left us in can't be trusted - in
        // particular, a byte-counted CIPRECVDATA binary capture (RXP_PAYLOAD)
        // has no way to tell a dropped byte from a real one, so it would
        // otherwise keep counting post-drop bytes (e.g. the next unsolicited
        // event's text) as if they were still part of the payload, completing
        // "successfully" with silently corrupted content instead of failing.
        // Resync on the next line and fail whatever command is in flight so
        // the caller retries instead of trusting corrupted data.
        rxp = RXP_LINE;
        line_len = 0;
        if (at_busy && !at_done) at_finish(false, -1);
    }

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

    char ssid_esc[SSID_LENGHT * 2];
    char psk_esc[PASS_LENGHT * 2];
    at_escape_quoted(ssid_esc, sizeof(ssid_esc), ssid);
    at_escape_quoted(psk_esc, sizeof(psk_esc), psk);

    bool ok = at_run_blocking(AT_CMD_CWJAP, -1, timeout_ms,
        "AT+CWJAP=\"%s\",\"%s\"", ssid_esc, psk_esc);
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

    char ssid_esc[SSID_LENGHT * 2];
    char psk_esc[PASS_LENGHT * 2];
    at_escape_quoted(ssid_esc, sizeof(ssid_esc), ssid);
    at_escape_quoted(psk_esc, sizeof(psk_esc), psk);

    // ecn=3 (WPA2_PSK), channel 1, up to 4 stations, broadcast SSID.
    return at_run_blocking(AT_CMD_PLAIN, -1, ESP_AT_TIMEOUT_BASIC_MS,
        "AT+CWSAP=\"%s\",\"%s\",1,3,4,0", ssid_esc, psk_esc);
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

    // Non-blocking, same tri-state shape as esp_at_tcp_connect(): an
    // AT+CIPSEND round-trip genuinely can't finish on its first call (OK,
    // then the '>' prompt, then SEND OK, spanning multiple polls), so a
    // legitimate "nothing sent *yet*" is 0, not an error - matching
    // mobile_func_sock_send()'s documented contract (mobile.h).
    //
    // This used to block internally instead (bounded by
    // ESP_AT_TIMEOUT_SEND_MS), because dependences/libmobile's relay.c and
    // dns.c both used to truncate mobile_cb_sock_send()'s int return to
    // bool, misreading a legitimate 0 as failure - confirmed on hardware to
    // break every relay connection through this backend. That's now fixed
    // upstream (relay.c's relay_send()/dns.c's resend path properly retry
    // on a partial/zero result instead of assuming atomicity - see the
    // submodule's "relay/dns: respect the non-blocking sock_send contract
    // instead of bool" commit), so the workaround is no longer needed. The
    // callers already in this codebase (socket_impl_send(), web/web_http.c)
    // were already written to tolerate a 0 return correctly - only this
    // function itself needed to stop hiding it.
    if (at_busy) {
        if (at_kind == AT_CMD_CIPSEND && at_link == link_id) {
            if (!at_done) return 0; // still in progress
            at_busy = false;
            return at_success ? at_result_code : -1;
        }
        return 0; // bus busy with something else; caller retries
    }

    if (size > ESP_AT_MAX_SEND_LEN) size = ESP_AT_MAX_SEND_LEN;

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
    // stay wedged forever waiting for a caller that will never come back.
    // The module doesn't know we gave up on that command though, and will
    // still eventually send its real response for it (SEND OK/ERROR/a
    // trailing OK/etc.) - confirmed on hardware to otherwise arrive later,
    // misattributed to whatever *new* command is in flight by then (e.g. a
    // stray "SEND OK" from an abandoned relay handshake send showing up
    // while a subsequent AT+CIPSTART retry is being processed). Give any
    // such straggler a short bounded window to arrive and be silently
    // discarded (process_line() only acts on a line while at_busy is true)
    // before issuing this close's own command - not a full fix (the module
    // could still reply after this window), just a best-effort reduction.
    if (at_busy && at_link == link_id) {
        at_busy = false;
        uint64_t drain_deadline = time_us_64() + MS(ESP_AT_ABANDON_DRAIN_MS);
        while (time_us_64() < drain_deadline) {
            uint8_t b;
            while (esp_uart_read_byte(&b)) feed_byte(b);
            sleep_ms(5);
        }
    }

    // Gate on in_use, not connected: a link whose AT+CIPSTART failed (or
    // never got far enough to see a "<id>,CONNECT" event) never sets
    // connected=true, but a real AT+CIPSTART was still sent for it - the
    // module may still consider that link reserved/half-open until an
    // explicit AT+CIPCLOSE, and the next attempt always reuses this same
    // fixed link ID (see esp_config.h's ESP_LINK_MOBILE_BASE comment).
    // Skipping CIPCLOSE here left a failed link dirty on the module's side,
    // making every subsequent AT+CIPSTART on it fail immediately too - e.g.
    // a relay connection retry loop that never recovers. A link that was
    // truly never opened at all (in_use=false) still gets no AT+CIPCLOSE:
    // sending one here would be harmless too, but there's nothing to clean
    // up. AT+CIPCLOSE on an already-closed link is itself a documented no-op
    // in ESP-AT, so being defensive here for the connected=true case (a real
    // established connection) costs nothing either.
    if (!links[link_id].in_use) { esp_at_link_release(link_id); return; }
    if (!at_busy) {
        uint32_t close_timeout = links[link_id].connected
            ? ESP_AT_TIMEOUT_CLOSE_MS : ESP_AT_TIMEOUT_CLOSE_UNCONFIRMED_MS;
        bool ok = at_run_blocking(AT_CMD_PLAIN, link_id, close_timeout, "AT+CIPCLOSE=%d", link_id);
        // Cosmetic parity with picow's socket_impl.c (which prints this from
        // its own tcp_close()/tcp_abort() outcome) - useful for the same
        // reason there: seeing a close actually happen (or not) in the log
        // when debugging a connection's lifetime.
        if (ok) {
            DEBUG_PRINT_FUNCTION("Socket Closed.");
        } else {
            DEBUG_PRINT_FUNCTION("Socket close failed (link %d, AT+CIPCLOSE error/timeout).", link_id);
        }
    }
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
