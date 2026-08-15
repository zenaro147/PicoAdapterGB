#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lwip/tcp.h"

#include "globals.h"

// No auth: this server is only reachable on the adapter's own trusted WiFi
// network (or its fallback hotspot), same trust boundary as the serial menu.

#define WEB_MAX_CONNS      2
#define WEB_REQ_BUF_SIZE   1536
#define WEB_RESP_BUF_SIZE  8192
#define WEB_REBOOT_DELAY_MS 300
#define EEPROM_FILE_SIZE 512
#define EEPROM_ORIGINAL_CONFIG_SIZE 0xC0

struct web_conn {
    struct tcp_pcb *pcb;
    bool in_use;
    char req_buf[WEB_REQ_BUF_SIZE];
    int req_len;
    char resp_buf[WEB_RESP_BUF_SIZE];
    int resp_len;
    int resp_sent;
    bool response_ready;
};

// Set once by web_config_start()/web_config_run_blocking(); every route
// handler operates on this single adapter instance.
extern struct mobile_user *web_mobile;

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

// Re-reads the persisted Wi-Fi credentials from flash into RAM. Does not
// touch the libmobile config, which already lives entirely in RAM/EEPROM.
void web_reload_saved_config(struct mobile_user *mobile);
