// HTTP engine for the config server: connection lifecycle, request parsing,
// response framing and dispatch to web_routes.h handlers. No knowledge of
// what any given route actually does lives here.
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

// Allocated only while the config server is running (see web_config_listen /
// web_config_stop): ~21KB across the two slots would otherwise sit unused in
// SRAM for the entire time the Game Boy is being used.
static struct web_conn *web_conns = NULL;
struct mobile_user *web_mobile = NULL;
static struct tcp_pcb *web_listen_pcb = NULL;

static struct web_conn *web_find_free_slot(void){
    if (!web_conns) return NULL;
    for (int i = 0; i < WEB_MAX_CONNS; i++){
        if (!web_conns[i].in_use) return &web_conns[i];
    }
    return NULL;
}

static void web_close_conn(struct web_conn *c){
    if (c->pcb){
        tcp_arg(c->pcb, NULL);
        tcp_recv(c->pcb, NULL);
        tcp_sent(c->pcb, NULL);
        tcp_err(c->pcb, NULL);
        if (tcp_close(c->pcb) != ERR_OK) tcp_abort(c->pcb);
    }
    c->pcb = NULL;
    c->in_use = false;
}

static void web_flush(struct web_conn *c){
    if (!c->pcb) return;
    int remain = c->resp_len - c->resp_sent;
    if (remain <= 0) return;
    u16_t avail = tcp_sndbuf(c->pcb);
    int chunk = remain < avail ? remain : avail;
    if (chunk <= 0) return;
    if (tcp_write(c->pcb, c->resp_buf + c->resp_sent, chunk, TCP_WRITE_FLAG_COPY) == ERR_OK){
        tcp_output(c->pcb);
        c->resp_sent += chunk;
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
    web_flush(c);
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
    web_flush(c);
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

void web_reload_saved_config(struct mobile_user *mobile){
    if (!mobile) return;

    memset(mobile->wifiSSID, 0, sizeof(mobile->wifiSSID));
    memset(mobile->wifiPASS, 0, sizeof(mobile->wifiPASS));

    struct saved_data_pointers ptrs;
    InitSavedPointers(&ptrs, mobile);
    ReadConfig(&ptrs);
    mobile_config_load(mobile->adapter);
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

static err_t web_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len){
    (void)pcb; (void)len;
    struct web_conn *c = (struct web_conn *)arg;
    if (!c) return ERR_OK;
    web_flush(c);
    if (c->response_ready && c->resp_sent >= c->resp_len) web_close_conn(c);
    return ERR_OK;
}

static void web_err_cb(void *arg, err_t err){
    (void)err;
    struct web_conn *c = (struct web_conn *)arg;
    if (c){
        c->pcb = NULL;
        c->in_use = false;
    }
}

static err_t web_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err){
    (void)err;
    struct web_conn *c = (struct web_conn *)arg;
    if (!c){
        if (p) pbuf_free(p);
        return ERR_OK;
    }
    if (!p){
        web_close_conn(c);
        return ERR_OK;
    }

    int copy_len = p->tot_len;
    if (c->req_len + copy_len >= WEB_REQ_BUF_SIZE) copy_len = WEB_REQ_BUF_SIZE - 1 - c->req_len;
    if (copy_len > 0){
        pbuf_copy_partial(p, c->req_buf + c->req_len, copy_len, 0);
        c->req_len += copy_len;
        c->req_buf[c->req_len] = '\0';
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (!c->response_ready) web_process(c);
    return ERR_OK;
}

static err_t web_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err){
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;

    struct web_conn *slot = web_find_free_slot();
    if (!slot){
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    memset(slot, 0, sizeof(*slot));
    slot->pcb = newpcb;
    slot->in_use = true;
    tcp_arg(newpcb, slot);
    tcp_recv(newpcb, web_recv_cb);
    tcp_sent(newpcb, web_sent_cb);
    tcp_err(newpcb, web_err_cb);
    return ERR_OK;
}

// Shared setup used by both the non-blocking (always-on) and blocking
// (hotspot fallback) modes. Returns the listen pcb, or NULL on failure.
static struct tcp_pcb *web_config_listen(struct mobile_user *mobile){
    web_mobile = mobile;
    if (!web_conns) web_conns = malloc(WEB_MAX_CONNS * sizeof(struct web_conn));
    if (!web_conns) return NULL;
    memset(web_conns, 0, WEB_MAX_CONNS * sizeof(struct web_conn));

    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) return NULL;
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK){
        tcp_close(pcb);
        return NULL;
    }
    struct tcp_pcb *listen_pcb = tcp_listen_with_backlog(pcb, 4);
    if (!listen_pcb) return NULL;
    tcp_accept(listen_pcb, web_accept_cb);
    return listen_pcb;
}

// Starts the server and returns immediately. The caller's own loop must keep
// calling net_poll() for the accept/recv/sent callbacks to fire.
void web_config_start(struct mobile_user *mobile){
    web_listen_pcb = web_config_listen(mobile);
}

// Tears down the listening socket and any open connections. Safe to call
// once; the server never restarts on its own afterwards.
void web_config_stop(void){
    if (!web_listen_pcb) return;

    tcp_arg(web_listen_pcb, NULL);
    tcp_accept(web_listen_pcb, NULL);
    tcp_close(web_listen_pcb);
    web_listen_pcb = NULL;

    if (web_conns){
        for (int i = 0; i < WEB_MAX_CONNS; i++){
            if (web_conns[i].in_use) web_close_conn(&web_conns[i]);
        }
        free(web_conns);
        web_conns = NULL;
    }
}

// Used only for the hotspot config fallback, where nothing else is running.
// There is no valid network config to continue with, so this never returns:
// the only way out is the user rebooting via the web page (hardware watchdog).
void web_config_run_blocking(struct mobile_user *mobile){
    if (!web_config_listen(mobile)) return;

    while (true){
        net_poll();
    }
}
