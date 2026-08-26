// HTTP engine for the config server: connection lifecycle, request parsing,
// response framing and dispatch to web_routes.h handlers. No knowledge of
// what any given route actually does lives here.
//
// Unlike picow's web_http.c (event-driven: lwIP calls back into this file as
// bytes arrive/room frees up), there are no ESP-AT callbacks - every
// connection's accept/recv/send progress is driven by explicitly polling
// net/esp_at.h from web_service_conns(), called once per tick from
// web_config_service_pending_actions() (see src/main.c's main loop).
#include "web_internal.h"
#include "web_routes.h"
#include "web_page.h"
#include "web_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "net/net_hal.h"
#include "storage/flash_eeprom.h"
#include "core/led_status.h"
#include "core/adapter_bridge.h"
#include "hardware/watchdog.h"

#include "net/esp_at.h"

// Allocated only while the config server is running (see web_config_listen /
// web_config_stop): ~21KB across the two slots would otherwise sit unused in
// SRAM for the entire time the Game Boy is being used.
static struct web_conn *web_conns = NULL;
struct mobile_user *web_mobile = NULL;
struct mobile_user *web_mobile_snapshot = NULL;
static bool web_server_running = false;
static volatile bool web_save_reboot_pending = false;
static bool web_reboot_waiting = false;
static uint64_t web_reboot_deadline = 0;
static uint64_t web_save_not_before = 0;

static struct web_conn *web_find_free_slot(void){
    if (!web_conns) return NULL;
    for (int i = 0; i < WEB_MAX_CONNS; i++){
        if (!web_conns[i].in_use) return &web_conns[i];
    }
    return NULL;
}

static void web_close_conn(struct web_conn *c){
    if (c->link_id >= 0) esp_at_close(c->link_id);
    c->link_id = -1;
    c->in_use = false;
    c->close_pending = false;
}

static void web_request_close(struct web_conn *c){
    if (c) c->close_pending = true;
}

static void web_service_pending_closes(void){
    if (!web_conns) return;
    for (int i = 0; i < WEB_MAX_CONNS; i++){
        if (web_conns[i].in_use && web_conns[i].close_pending) {
            web_close_conn(&web_conns[i]);
        }
    }
}

void web_send_response(struct web_conn *c, int status, const char *status_text,
                        const char *content_type, const char *body){
    size_t full_body_len = strlen(body);

    // Build the header separately first so its real length (including the
    // Content-Length digits) is known before deciding how much of the body
    // fits, instead of estimating from a shorter header without it.
    char header[128];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        status, status_text, content_type, full_body_len);
    if (header_len < 0) header_len = 0;
    if ((size_t)header_len >= sizeof(header)) header_len = sizeof(header) - 1;

    size_t max_body = (size_t)header_len < WEB_RESP_BUF_SIZE ? WEB_RESP_BUF_SIZE - (size_t)header_len : 0;
    size_t body_len = full_body_len > max_body ? max_body : full_body_len;
    if (body_len != full_body_len){
        // Truncated: Content-Length must reflect what's actually sent.
        header_len = snprintf(header, sizeof(header),
            "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
            status, status_text, content_type, body_len);
        if (header_len < 0) header_len = 0;
        if ((size_t)header_len >= sizeof(header)) header_len = sizeof(header) - 1;
    }

    memcpy(c->resp_buf, header, (size_t)header_len);
    memcpy(c->resp_buf + header_len, body, body_len);
    c->resp_len = header_len + (int)body_len;
    c->resp_sent = 0;
    c->response_ready = true;
}

void web_send_raw_response(struct web_conn *c, int status, const char *status_text,
                            const char *content_type, const void *body, size_t body_len){
    size_t max_body = WEB_RESP_BUF_SIZE - 256;
    if (body_len > max_body) body_len = max_body;

    size_t header_len = snprintf(c->resp_buf, WEB_RESP_BUF_SIZE,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        status, status_text, content_type, body_len);
    if (header_len >= WEB_RESP_BUF_SIZE) header_len = WEB_RESP_BUF_SIZE - 1;
    memcpy(c->resp_buf + header_len, body, body_len);
    c->resp_len = (int)(header_len + body_len);
    c->resp_sent = 0;
    c->response_ready = true;
}

void web_json_escape(const char *src, char *dst, size_t dstsize){
    size_t j = 0;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (size_t i = 0; src[i] && j + 2 < dstsize; i++){
        unsigned char ch = (unsigned char)src[i];
        switch (ch){
            case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
            case '"': dst[j++] = '\\'; dst[j++] = '"'; break;
            case '\n': dst[j++] = '\\'; dst[j++] = 'n'; break;
            case '\r': dst[j++] = '\\'; dst[j++] = 'r'; break;
            case '\t': dst[j++] = '\\'; dst[j++] = 't'; break;
            default:
                if (ch < 0x20) {
                    j += snprintf(dst + j, dstsize - j, "\\u%04x", ch);
                } else {
                    dst[j++] = (char)ch;
                }
                break;
        }
    }
    dst[j] = '\0';
}

const char *web_stristr(const char *hay, const char *needle){
    size_t nlen = strlen(needle);
    for (; *hay; hay++){
        if (strncasecmp(hay, needle, nlen) == 0) return hay;
    }
    return NULL;
}

static void url_decode(char *dst, size_t dstsize, const char *src, size_t srclen){
    size_t di = 0;
    for (size_t i = 0; i < srclen && di < dstsize - 1; i++){
        char ch = src[i];
        if (ch == '+'){
            dst[di++] = ' ';
        } else if (ch == '%' && i + 2 < srclen){
            char hex[3] = { src[i+1], src[i+2], '\0' };
            dst[di++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            dst[di++] = ch;
        }
    }
    dst[di] = '\0';
}

// Returns true if the key was present in the urlencoded body (even if empty).
bool web_form_get(const char *body, const char *key, char *out, size_t outsize){
    size_t keylen = strlen(key);
    const char *p = body;
    while (*p){
        const char *eq = strchr(p, '=');
        if (!eq) break;
        size_t namelen = eq - p;
        const char *amp = strchr(eq, '&');
        size_t vallen = amp ? (size_t)(amp - eq - 1) : strlen(eq + 1);
        if (namelen == keylen && strncmp(p, key, keylen) == 0){
            url_decode(out, outsize, eq + 1, vallen);
            return true;
        }
        if (!amp) break;
        p = amp + 1;
    }
    out[0] = '\0';
    return false;
}

// Parses a hex string and stores in buf. Returns true on success, false if invalid format or buffer too small.
// Accepts: '0'-'9', 'A'-'F', 'a'-'f'. Rejects any other character (e.g., '@', '$', 'Z').
bool web_parse_hex(unsigned char *buf, char *str, unsigned size) {
    unsigned char x = 0;
    for (unsigned i = 0; i < size * 2; i++) {
        char c = str[i];
        if (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'A' && c <= 'F') c -= 'A' - 10;
        else if (c >= 'a' && c <= 'f') c -= 'a' - 10;
        else return false;

        x <<= 4;
        x |= c;

        if (i % 2 == 1) {
            buf[i / 2] = x;
            x = 0;
        }
    }
    return true;
}

// Formats just the host, without the port (the port is edited in its own field).
void web_format_addr_ip_only(struct mobile_addr *src, char *dest, size_t destsize){
    struct mobile_addr4 *addr4 = (struct mobile_addr4 *)src;
    struct mobile_addr6 *addr6 = (struct mobile_addr6 *)src;
    dest[0] = '\0';
    switch (src->type){
        case MOBILE_ADDRTYPE_IPV4:
            snprintf(dest, destsize, "%i.%i.%i.%i",
                addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3]);
            break;
        case MOBILE_ADDRTYPE_IPV6:
            snprintf(dest, destsize,
                "%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx",
                addr6->host[0], addr6->host[1], addr6->host[2], addr6->host[3],
                addr6->host[4], addr6->host[5], addr6->host[6], addr6->host[7],
                addr6->host[8], addr6->host[9], addr6->host[10], addr6->host[11],
                addr6->host[12], addr6->host[13], addr6->host[14], addr6->host[15]);
            break;
        default:
            break;
    }
}

// Parses IP address string and stores in dest. Returns 1 on success, 0 if invalid format.
int web_parse_addr(struct mobile_addr *dest, char *argv) {
    if (!dest || !argv[0]) return 0;

    unsigned char ip[MOBILE_INET_PTON_MAXLEN];
    int rc = mobile_inet_pton(MOBILE_INET_PTON_ANY, argv, ip);

    struct mobile_addr4 *addr4 = (struct mobile_addr4 *)dest;
    struct mobile_addr6 *addr6 = (struct mobile_addr6 *)dest;

    switch (rc) {
        case MOBILE_INET_PTON_IPV4:
            addr4->type = MOBILE_ADDRTYPE_IPV4;
            memcpy(addr4->host, ip, sizeof(addr4->host));
            return 1;
        case MOBILE_INET_PTON_IPV6:
            addr6->type = MOBILE_ADDRTYPE_IPV6;
            memcpy(addr6->host, ip, sizeof(addr6->host));
            return 1;
        default:
            printf("Invalid IP address\n");
            return 0;
    }
}

// Sets the port on an already-parsed mobile_addr (IPv4 or IPv6). Returns void.
void web_set_addr_port(struct mobile_addr *dest, unsigned port) {
    struct mobile_addr4 *addr4 = (struct mobile_addr4 *)dest;
    struct mobile_addr6 *addr6 = (struct mobile_addr6 *)dest;

    switch (dest->type) {
        case MOBILE_ADDRTYPE_IPV4:
            addr4->port = port;
            return;
        case MOBILE_ADDRTYPE_IPV6:
            addr6->port = port;
            return;
        default:
            printf("Invalid Port\n");
            return;
    }
}

// Tears down the standalone snapshot adapter (see web_snapshot_create()).
// Safe to call when no snapshot exists.
static void web_snapshot_destroy(void){
    if (!web_mobile_snapshot) return;
    free(web_mobile_snapshot->adapter);
    free(web_mobile_snapshot);
    web_mobile_snapshot = NULL;
}

// Takes a fresh copy of *mobile - config_eeprom, Wi-Fi credentials, etc. -
// and gives it its own standalone struct mobile_adapter, so every config
// read/write route can operate on the copy without ever touching the live
// adapter mobile_loop() is driving. The copy's adapter is deliberately never
// started/looped: it only exists so mobile_config_get_*/set_*() have
// somewhere to read and write that isn't the live adapter's in-progress
// state (see adapter_bridge_register_snapshot_callbacks()).
static void web_snapshot_create(struct mobile_user *mobile){
    web_snapshot_destroy();
    if (!mobile) return;

    web_mobile_snapshot = malloc(sizeof(struct mobile_user));
    if (!web_mobile_snapshot) return;

    *web_mobile_snapshot = *mobile;
    web_mobile_snapshot->adapter = mobile_new(web_mobile_snapshot);
    adapter_bridge_register_snapshot_callbacks(web_mobile_snapshot->adapter);
    mobile_config_load(web_mobile_snapshot->adapter);
}

static void web_dispatch(struct web_conn *c, bool is_get, bool is_post, const char *path, const char *body, int content_length){
    if (is_get && strcmp(path, "/") == 0){
        web_send_response(c, 200, "OK", "text/html", WEB_CONFIG_HTML);
    } else if (is_get && strcmp(path, "/api/config") == 0){
        handle_get_config(c);
    } else if (is_get && strcmp(path, "/api/relay_number") == 0){
        handle_get_relay_number(c);
    } else if (is_get && strcmp(path, "/api/eeprom") == 0){
        handle_get_eeprom(c);
    } else if (is_post && strcmp(path, "/api/config") == 0){
        handle_post_config(c, body);
    } else if (is_post && strcmp(path, "/api/eeprom") == 0){
        handle_post_eeprom(c, body, content_length);
    } else if (is_post && strcmp(path, "/api/format") == 0){
        handle_post_format(c);
    } else if (is_post && strcmp(path, "/api/reboot") == 0){
        handle_post_reboot(c);
    } else {
        web_send_response(c, 404, "Not Found", "text/plain", "Not found");
    }
}

static void web_process(struct web_conn *c){
    char *hdr_end = strstr(c->req_buf, "\r\n\r\n");
    if (!hdr_end) return; // headers not fully received yet

    int header_len = (hdr_end - c->req_buf) + 4;
    bool is_post = strncmp(c->req_buf, "POST ", 5) == 0;
    bool is_get = strncmp(c->req_buf, "GET ", 4) == 0;

    if (!is_post && !is_get){
        web_send_response(c, 405, "Method Not Allowed", "text/plain", "Method not allowed");
        return;
    }

    char path[64] = {0};
    const char *p = c->req_buf + (is_post ? 5 : 4);
    int i = 0;
    while (*p && *p != ' ' && *p != '?' && i < (int)sizeof(path) - 1) path[i++] = *p++;
    path[i] = '\0';

    int content_length = 0;
    const char *cl = web_stristr(c->req_buf, "Content-Length:");
    if (cl) content_length = atoi(cl + strlen("Content-Length:"));

    if (is_post){
        int body_have = c->req_len - header_len;
        if (content_length < 0) content_length = 0;
        if (header_len + content_length >= WEB_REQ_BUF_SIZE) content_length = WEB_REQ_BUF_SIZE - header_len - 1;
        if (body_have < content_length) return; // wait for the rest of the body
    }

    char *body = c->req_buf + header_len;
    body[content_length] = '\0';

    web_dispatch(c, is_get, is_post, path, body, content_length);
}

// ---------------------------------------------------------------------
// Polling-driven connection lifecycle (see file header comment).
// ---------------------------------------------------------------------

static void web_service_conns(void){
    if (!web_conns) return;

    // Accept: claim any incoming connections the shared CIPSERVER slot has
    // queued for us (see esp_at.h - only relevant while we actually own it).
    if (esp_at_server_owner() == ESP_LINK_OWNER_WEB){
        struct web_conn *slot;
        int link_id;
        while ((slot = web_find_free_slot()) != NULL &&
               (link_id = esp_at_server_accept(ESP_LINK_OWNER_WEB)) >= 0){
            memset(slot, 0, sizeof(*slot));
            slot->link_id = link_id;
            slot->in_use = true;
        }
    }

    for (int i = 0; i < WEB_MAX_CONNS; i++){
        struct web_conn *c = &web_conns[i];
        if (!c->in_use || c->close_pending) continue;

        bool remote_closed = esp_at_link_consume_closed_event(c->link_id);

        if (!c->response_ready){
            int room = WEB_REQ_BUF_SIZE - 1 - c->req_len;
            if (room > 0){
                int want = room < WEB_RECV_CHUNK ? room : WEB_RECV_CHUNK;
                int got = esp_at_recv(c->link_id, c->recv_chunk, (unsigned)want);
                if (got > 0){
                    memcpy(c->req_buf + c->req_len, c->recv_chunk, (size_t)got);
                    c->req_len += got;
                    c->req_buf[c->req_len] = '\0';
                    web_process(c);
                } else if (got < 0){
                    web_request_close(c);
                    continue;
                }
            } else {
                // Request too large for the buffer: nothing sane to do with it.
                web_send_response(c, 413, "Payload Too Large", "text/plain", "Request too large");
            }
        }

        if (c->response_ready && c->resp_sent < c->resp_len){
            int remain = c->resp_len - c->resp_sent;
            int rc = esp_at_send(c->link_id, c->resp_buf + c->resp_sent, (unsigned)remain);
            if (rc > 0) c->resp_sent += rc;
            else if (rc < 0) { web_request_close(c); continue; }
        }

        if (c->response_ready && c->resp_sent >= c->resp_len){
            // The response carries Connection: close - nothing else to do
            // with this link once it's fully written.
            web_request_close(c);
            continue;
        }

        if (remote_closed && esp_at_link_rx_pending(c->link_id) == 0) web_request_close(c);
    }
}

// Shared setup used by both the non-blocking (always-on) and blocking
// (hotspot fallback) modes. Returns true on success.
static bool web_config_listen(struct mobile_user *mobile){
    web_mobile = mobile;
    web_snapshot_create(mobile);
    if (!web_conns) web_conns = malloc(WEB_MAX_CONNS * sizeof(struct web_conn));
    if (!web_conns) return false;
    memset(web_conns, 0, WEB_MAX_CONNS * sizeof(struct web_conn));
    for (int i = 0; i < WEB_MAX_CONNS; i++) web_conns[i].link_id = -1;

    if (!esp_at_server_start(80, ESP_LINK_OWNER_WEB)) return false;
    web_server_running = true;
    return true;
}

// Starts the server and returns immediately. The caller's own loop must keep
// calling web_config_service_pending_actions() to drive accept/recv/send.
void web_config_start(struct mobile_user *mobile){
    web_config_listen(mobile);
}

// Tears down the listening socket and any open connections. Safe to call
// once; the server never restarts on its own afterwards.
void web_config_stop(void){
    if (!web_server_running) return;

    esp_at_server_stop(ESP_LINK_OWNER_WEB);
    web_server_running = false;

    if (web_conns){
        for (int i = 0; i < WEB_MAX_CONNS; i++){
            if (web_conns[i].in_use) web_close_conn(&web_conns[i]);
        }
        free(web_conns);
        web_conns = NULL;
    }

    // The config snapshot only exists for the lifetime of the web UI.
    web_snapshot_destroy();
}

void web_config_request_save_reboot(void){
    web_save_reboot_pending = true;
    web_save_not_before = time_us_64() + MS(500);
}

void web_config_service_pending_actions(void){
    if (web_server_running) web_service_conns();
    web_service_pending_closes();

    if (web_reboot_waiting){
        if (time_us_64() < web_reboot_deadline) return;

        DEBUG_PRINT_FUNCTION("Reboot delay complete. Resetting device...");
        watchdog_enable(WEB_REBOOT_DELAY_MS, 0);
        watchdog_update();
        while (true) tight_loop_contents();
    }

    if (!web_save_reboot_pending || !web_mobile_snapshot || time_us_64() < web_save_not_before) return;

    web_save_reboot_pending = false;
    DEBUG_PRINT_FUNCTION("Web Save & Reboot requested. Saving configuration...");

    // Persist the snapshot the web UI has been editing, not the live
    // web_mobile: the live adapter's config is only supposed to change on the
    // next boot, when it re-reads this same flash data.
    struct saved_data_pointers save_ptrs;
    InitSavedPointers(&save_ptrs, web_mobile_snapshot);
    if (!SaveConfig(&save_ptrs)) {
        led_status_report_error(LED_ERROR_FLASH_SAVE_FAILED);
    }
    DEBUG_PRINT_FUNCTION("Configuration saved. Waiting for HTTP response to finish...");

    web_reboot_waiting = true;
    web_reboot_deadline = time_us_64() + MS(1000);
}

// Used only for the hotspot config fallback, where nothing else is running.
// There is no valid network config to continue with, so this never returns:
// the only way out is the user rebooting via the web page (hardware watchdog).
void web_config_run_blocking(struct mobile_user *mobile){
    if (!web_config_listen(mobile)) return;

    // The fallback hotspot's own "boot" is complete now that its setup page
    // is reachable; hand the LED to the runtime "config to save" indicator.
    led_status_boot_done();

    while (true){
        net_poll();
        web_config_service_pending_actions();
    }
}
