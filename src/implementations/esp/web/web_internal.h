#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "globals.h"

// No auth: this server is only reachable on the adapter's own trusted WiFi
// network (or its fallback hotspot), same trust boundary as the serial menu.
//
// Unlike picow's web_internal.h (whose struct web_conn wraps a live lwIP
// struct tcp_pcb *, driven by lwIP's own accept/recv/sent callbacks), this
// backend has no callback mechanism: ESP-AT only ever tells us about link
// state through esp_at_poll()'s event parsing (see net/esp_at.h). So
// struct web_conn here just tracks an ESP-AT link ID, and web_http.c drives
// every connection's state machine (accept/recv/send/close) by polling
// net/esp_at.h once per tick from web_config_service_pending_actions() -
// there is no equivalent of picow's web_accept_cb()/web_recv_cb() firing on
// their own between ticks.

#define WEB_MAX_CONNS      2
#define WEB_REQ_BUF_SIZE   1536
#define WEB_RESP_BUF_SIZE  9216
#define WEB_REBOOT_DELAY_MS 300
#define EEPROM_FILE_SIZE 512
#define EEPROM_ORIGINAL_CONFIG_SIZE 0xC0

struct web_conn {
    int link_id;    // ESP-AT link ID owning this HTTP connection, -1 if free
    bool in_use;
    char req_buf[WEB_REQ_BUF_SIZE];
    int req_len;
    char resp_buf[WEB_RESP_BUF_SIZE];
    int resp_len;
    int resp_sent;
    bool response_ready;
    bool close_pending;
};

// Set once by web_config_start()/web_config_run_blocking(): the live adapter
// instance the Game Boy actually talks to. Route handlers must not call
// mobile_config_set_*()/mobile_config_save() on this one - only read it, and
// only for state the snapshot can never have (e.g. the relay-assigned number
// in web_mobile->number_user, which only a live relay session can obtain).
extern struct mobile_user *web_mobile;

// A copy of *web_mobile taken when the web UI starts (see web_config_listen()
// in web_http.c), with its own standalone struct mobile_adapter that is never
// started/looped. Every config read/write route operates on this instead of
// web_mobile, so editing settings in the browser can't affect the live
// adapter's in-progress relay/DNS/session behavior. Save & Reboot is the only
// place that copies the snapshot's bytes out to flash; the live adapter picks
// them up fresh on the next boot. Freed and recreated whenever the web server
// (re)starts - it only exists for the lifetime of the web UI.
extern struct mobile_user *web_mobile_snapshot;

void web_config_request_save_reboot(void);

void web_send_response(struct web_conn *c, int status, const char *status_text,
    const char *content_type, const char *body);
void web_send_raw_response(struct web_conn *c, int status, const char *status_text,
    const char *content_type, const void *body, size_t body_len);

const char *web_stristr(const char *hay, const char *needle);
void web_json_escape(const char *src, char *dst, size_t dstsize);
bool web_form_get(const char *body, const char *key, char *out, size_t outsize);
bool web_parse_hex(unsigned char *buf, char *str, unsigned size);
void web_format_addr_ip_only(struct mobile_addr *src, char *dest, size_t destsize);
int web_parse_addr(struct mobile_addr *dest, char *argv);
void web_set_addr_port(struct mobile_addr *dest, unsigned port);
