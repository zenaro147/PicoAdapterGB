#include "web_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lwip/tcp.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"

#include "flash_eeprom.h"

// No auth: this server is only reachable on the adapter's own trusted WiFi
// network (or its fallback hotspot), same trust boundary as the serial menu.

#define WEB_MAX_CONNS      2
#define WEB_REQ_BUF_SIZE   1536
#define WEB_RESP_BUF_SIZE  4096
#define WEB_REBOOT_DELAY_MS 300

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

static struct web_conn web_conns[WEB_MAX_CONNS];
static struct mobile_user *web_mobile = NULL;
static struct tcp_pcb *web_listen_pcb = NULL;

static const char WEB_CONFIG_HTML[] =
"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
"<title>PicoAdapterGB Config</title>"
"<style>"
"body{font-family:sans-serif;max-width:640px;margin:2em auto;padding:0 1em;}"
"fieldset{margin-bottom:1em;}label{display:block;margin-top:.5em;}"
"input[type=text],input[type=password],input[type=number],select{width:100%;box-sizing:border-box;}"
"button{margin-top:1em;padding:.5em 1em;}#msg{margin-top:1em;font-weight:bold;}"
"</style></head><body>"
"<h1>PicoAdapterGB - Setup</h1>"
"<form id=\"cfg\">"
"<fieldset><legend>WiFi</legend>"
"<label>SSID<input type=\"text\" id=\"wifi_ssid\"></label>"
"<label>Password<input type=\"password\" id=\"wifi_pass\"></label>"
"</fieldset>"
"<fieldset><legend>DNS</legend>"
"<label>DNS 1 (IP)<input type=\"text\" id=\"dns1\"></label>"
"<label>DNS 2 (IP)<input type=\"text\" id=\"dns2\"></label>"
"<label>DNS Port<input type=\"number\" id=\"dns_port\"></label>"
"</fieldset>"
"<fieldset><legend>Relay (P2P)</legend>"
"<label>Relay Server (ip)<input type=\"text\" id=\"relay\"></label>"
"<label>Relay Token (32 hex chars)<input type=\"text\" id=\"relay_token\"></label>"
"<label>P2P Port<input type=\"number\" id=\"p2p_port\"></label>"
"</fieldset>"
"<fieldset><legend>Adapter</legend>"
"<label>Device<select id=\"device\">"
"<option value=\"BLUE\">Blue (PDC)</option>"
"<option value=\"YELLOW\">Yellow (cdmaOne)</option>"
"<option value=\"GREEN\">Green (PHS-NTT)</option>"
"<option value=\"RED\">Red (DDI)</option>"
"</select></label>"
"<label><input type=\"checkbox\" id=\"unmetered\"> Unmetered (Pokemon Crystal)</label>"
"<label><input type=\"checkbox\" id=\"redirect_mail\"> Redirect SMTP to alt port</label>"
"</fieldset>"
"<fieldset><legend>Mobile Adapter EEPROM (.bin) (Not implemented yet)</legend>"
"<label>Upload eeprom.bin<input type=\"file\" id=\"eeprom_file\" accept=\".bin\"></label>"
"<button type=\"button\" id=\"eeprom_upload\">Upload eeprom.bin</button>"
"<button type=\"button\" id=\"eeprom_dump\">Download eeprom.bin</button>"
"</fieldset>"
"<button type=\"submit\">Save &amp; Reboot</button>"
"<button type=\"button\" id=\"fmt\">Format EEPROM</button>"
"</form>"
"<div id=\"msg\"></div>"
"<div id=\"ver\"></div>"
"<script>"
"async function load(){"
"const r=await fetch('/api/config');const j=await r.json();"
"wifi_ssid.value=j.wifi_ssid;wifi_pass.value=j.wifi_pass;dns1.value=j.dns1;dns2.value=j.dns2;dns_port.value=j.dns_port;"
"relay.value=j.relay;relay_token.value=j.relay_token;p2p_port.value=j.p2p_port;"
"device.value=j.device;unmetered.checked=j.unmetered;redirect_mail.checked=j.redirect_mail;"
"ver.textContent='libmobile '+j.libmobile_version+' / '+j.firmware_version;"
"}"
"function fields(){"
"const p=new URLSearchParams();"
"p.set('wifi_ssid',wifi_ssid.value);"
"p.set('wifi_pass',wifi_pass.value);"
"p.set('dns1',dns1.value);p.set('dns2',dns2.value);p.set('dns_port',dns_port.value);"
"p.set('relay',relay.value);p.set('relay_token',relay_token.value);p.set('p2p_port',p2p_port.value);"
"p.set('device',device.value);p.set('unmetered',unmetered.checked?'1':'0');"
"p.set('redirect_mail',redirect_mail.checked?'1':'0');"
"return p;"
"}"
"cfg.addEventListener('submit',async function(e){"
"e.preventDefault();"
"if(!confirm('Save and reboot the adapter now?'))return;"
"const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fields().toString()});"
"await r.json();"
"await fetch('/api/reboot',{method:'POST'});"
"msg.textContent='Saved. Rebooting...';"
"});"
"fmt.addEventListener('click',async function(){"
"if(!confirm('Format the EEPROM config? This resets adapter settings.'))return;"
"await fetch('/api/format',{method:'POST'});"
"msg.textContent='Formatted.';"
"load();"
"});"
"eeprom_upload.addEventListener('click',function(){"
"alert('Not implemented yet.');"
"});"
"eeprom_dump.addEventListener('click',function(){"
"alert('Not implemented yet.');"
"});"
"load();"
"</script>"
"</body></html>";

static struct web_conn *web_find_free_slot(void){
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

static void web_send_response(struct web_conn *c, int status, const char *status_text,
                               const char *content_type, const char *body){
    int body_len = strlen(body);
    c->resp_len = snprintf(c->resp_buf, WEB_RESP_BUF_SIZE,
        "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
        status, status_text, content_type, body_len, body);
    if (c->resp_len > WEB_RESP_BUF_SIZE) c->resp_len = WEB_RESP_BUF_SIZE;
    c->resp_sent = 0;
    c->response_ready = true;
    web_flush(c);
}

static const char *stristr(const char *hay, const char *needle){
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
static bool web_form_get(const char *body, const char *key, char *out, size_t outsize){
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
static bool web_parse_hex(unsigned char *buf, char *str, unsigned size) {
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
static void format_addr_ip_only(struct mobile_addr *src, char *dest, size_t destsize){
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

static void handle_get_config(struct web_conn *c){
    struct mobile_user *mobile = web_mobile;
    struct mobile_addr dns1 = {.type = MOBILE_ADDRTYPE_NONE};
    struct mobile_addr dns2 = {.type = MOBILE_ADDRTYPE_NONE};
    struct mobile_addr relay = {.type = MOBILE_ADDRTYPE_NONE};
    enum mobile_adapter_device device = MOBILE_ADAPTER_BLUE;
    bool unmetered = false;
    bool redirect_mail = true;
    unsigned p2p_port = MOBILE_DEFAULT_P2P_PORT;
    unsigned char token[MOBILE_RELAY_TOKEN_SIZE] = {0};

    mobile_config_get_dns(mobile->adapter, &dns1, MOBILE_DNS1);
    mobile_config_get_dns(mobile->adapter, &dns2, MOBILE_DNS2);
    mobile_config_get_relay(mobile->adapter, &relay);
    mobile_config_get_device(mobile->adapter, &device, &unmetered);
    mobile_config_get_alt_mail(mobile->adapter, &redirect_mail);
    mobile_config_get_p2p_port(mobile->adapter, &p2p_port);
    mobile_config_get_relay_token(mobile->adapter, token);

    char dns1str[60] = {0}, dns2str[60] = {0}, relaystr[60] = {0};
    format_addr_ip_only(&dns1, dns1str, sizeof(dns1str));
    format_addr_ip_only(&dns2, dns2str, sizeof(dns2str));
    format_addr_ip_only(&relay, relaystr, sizeof(relaystr));

    int dns_port = MOBILE_DNS_PORT;
    if (dns1.type == MOBILE_ADDRTYPE_IPV4) dns_port = dns1._addr4.port;
    else if (dns2.type == MOBILE_ADDRTYPE_IPV4) dns_port = dns2._addr4.port;

    char token_hex[MOBILE_RELAY_TOKEN_SIZE * 2 + 1];
    for (int i = 0; i < MOBILE_RELAY_TOKEN_SIZE; i++)
        sprintf(token_hex + i * 2, "%02hhX", token[i]);
    token_hex[MOBILE_RELAY_TOKEN_SIZE * 2] = '\0';

    const char *device_str = "BLUE";
    switch (device){
        case MOBILE_ADAPTER_YELLOW: device_str = "YELLOW"; break;
        case MOBILE_ADAPTER_GREEN:  device_str = "GREEN";  break;
        case MOBILE_ADAPTER_RED:    device_str = "RED";    break;
        default:                    device_str = "BLUE";   break;
    }

    char body[768];
    snprintf(body, sizeof(body),
        "{"
        "\"wifi_ssid\":\"%s\","
        "\"wifi_pass\":\"%s\","
        "\"dns1\":\"%s\","
        "\"dns2\":\"%s\","
        "\"dns_port\":%d,"
        "\"relay\":\"%s\","
        "\"relay_token\":\"%s\","
        "\"p2p_port\":%u,"
        "\"device\":\"%s\","
        "\"unmetered\":%s,"
        "\"redirect_mail\":%s,"
        "\"libmobile_version\":\"%u.%u.%u\","
        "\"firmware_version\":\"%s\""
        "}",
        mobile->wifiSSID, mobile->wifiPASS, dns1str, dns2str, dns_port, relaystr, token_hex, p2p_port,
        device_str, unmetered ? "true" : "false", redirect_mail ? "true" : "false",
        mobile_version_major, mobile_version_minor, mobile_version_patch,
        PICO_ADAPTER_SOFTWARE);

    web_send_response(c, 200, "OK", "application/json", body);
}

// Parses IP address string and stores in dest. Returns 1 on success, 0 if invalid format.
static int main_parse_addr(struct mobile_addr *dest, char *argv) {
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
static void main_set_port(struct mobile_addr *dest, unsigned port) {
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

static void handle_post_config(struct web_conn *c, const char *body){
    struct mobile_user *mobile = web_mobile;
    char field[128];
    bool needSave = false;

    if (web_form_get(body, "wifi_ssid", field, sizeof(field)) && field[0]
        && strlen(field) < SSID_LENGHT){
        memset(mobile->wifiSSID, 0, sizeof(mobile->wifiSSID));
        strncpy(mobile->wifiSSID, field, SSID_LENGHT - 1);
        needSave = true;
    }
    if (web_form_get(body, "wifi_pass", field, sizeof(field)) && field[0]
        && strlen(field) < PASS_LENGHT){
        memset(mobile->wifiPASS, 0, sizeof(mobile->wifiPASS));
        strncpy(mobile->wifiPASS, field, PASS_LENGHT - 1);
        needSave = true;
    }

    int dns_port = MOBILE_DNS_PORT;
    if (web_form_get(body, "dns_port", field, sizeof(field)) && field[0]){
        int v = atoi(field);
        if (v > 0) dns_port = v;
    }

    if (web_form_get(body, "dns1", field, sizeof(field))){
        struct mobile_addr dns1 = {.type = MOBILE_ADDRTYPE_NONE};
        if (field[0] && main_parse_addr(&dns1, field)) main_set_port(&dns1, dns_port);
        mobile_config_set_dns(mobile->adapter, &dns1, MOBILE_DNS1);
        needSave = true;
    }
    if (web_form_get(body, "dns2", field, sizeof(field))){
        struct mobile_addr dns2 = {.type = MOBILE_ADDRTYPE_NONE};
        if (field[0] && main_parse_addr(&dns2, field)) main_set_port(&dns2, dns_port);
        mobile_config_set_dns(mobile->adapter, &dns2, MOBILE_DNS2);
        needSave = true;
    }
    if (web_form_get(body, "relay", field, sizeof(field))){
        struct mobile_addr relay = {.type = MOBILE_ADDRTYPE_NONE};
        if (field[0] && main_parse_addr(&relay, field)) main_set_port(&relay, MOBILE_DEFAULT_RELAY_PORT);
        mobile_config_set_relay(mobile->adapter, &relay);
        needSave = true;
    }
    if (web_form_get(body, "relay_token", field, sizeof(field))){
        if (field[0]){
            unsigned char token_buf[MOBILE_RELAY_TOKEN_SIZE];
            if (web_parse_hex(token_buf, field, sizeof(token_buf))){
                mobile_config_set_relay_token(mobile->adapter, token_buf);
                needSave = true;
            }
        } else {
            mobile_config_set_relay_token(mobile->adapter, NULL);
            needSave = true;
        }
    }
    if (web_form_get(body, "p2p_port", field, sizeof(field)) && field[0]){
        int v = atoi(field);
        if (v > 0){
            mobile_config_set_p2p_port(mobile->adapter, v);
            needSave = true;
        }
    }

    enum mobile_adapter_device cur_device = MOBILE_ADAPTER_BLUE;
    bool cur_unmetered = false;
    mobile_config_get_device(mobile->adapter, &cur_device, &cur_unmetered);

    if (web_form_get(body, "device", field, sizeof(field)) && field[0]){
        if (strcmp(field, "BLUE") == 0) cur_device = MOBILE_ADAPTER_BLUE;
        else if (strcmp(field, "YELLOW") == 0) cur_device = MOBILE_ADAPTER_YELLOW;
        else if (strcmp(field, "GREEN") == 0) cur_device = MOBILE_ADAPTER_GREEN;
        else if (strcmp(field, "RED") == 0) cur_device = MOBILE_ADAPTER_RED;
        needSave = true;
    }
    if (web_form_get(body, "unmetered", field, sizeof(field))){
        cur_unmetered = (strcmp(field, "1") == 0 || strcmp(field, "true") == 0);
        needSave = true;
    }
    mobile_config_set_device(mobile->adapter, cur_device, cur_unmetered);

    if (web_form_get(body, "redirect_mail", field, sizeof(field))){
        bool redirect_mail = (strcmp(field, "1") == 0 || strcmp(field, "true") == 0);
        mobile_config_set_alt_mail(mobile->adapter, redirect_mail);
        needSave = true;
    }

    if (needSave){
        mobile_config_save(mobile->adapter);
        struct saved_data_pointers ptrs;
        InitSavedPointers(&ptrs, mobile);
        SaveConfig(&ptrs);
    }

    web_send_response(c, 200, "OK", "application/json", "{\"status\":\"ok\"}");
}

static void handle_post_format(struct web_conn *c){
    struct mobile_user *mobile = web_mobile;
    memset(mobile->config_eeprom, 0x00, sizeof(mobile->config_eeprom));

    mobile_config_set_dns(mobile->adapter, &(struct mobile_addr){.type = MOBILE_ADDRTYPE_NONE}, MOBILE_DNS1);
    mobile_config_set_dns(mobile->adapter, &(struct mobile_addr){.type = MOBILE_ADDRTYPE_NONE}, MOBILE_DNS2);
    mobile_config_set_relay(mobile->adapter, &(struct mobile_addr){.type = MOBILE_ADDRTYPE_NONE});
    mobile_config_set_relay_token(mobile->adapter, NULL);
    mobile_config_set_p2p_port(mobile->adapter, MOBILE_DEFAULT_P2P_PORT);
    mobile_config_set_device(mobile->adapter, MOBILE_ADAPTER_BLUE, false);
    mobile_config_save(mobile->adapter);

    memset(mobile->wifiSSID, 0x00, sizeof(mobile->wifiSSID));
    memset(mobile->wifiPASS, 0x00, sizeof(mobile->wifiPASS));
    strcpy(mobile->wifiSSID, "WiFi_Network");
    strcpy(mobile->wifiPASS, "P@$$w0rd");
    mobile_config_save(mobile->adapter);

    struct saved_data_pointers ptrs;
    InitSavedPointers(&ptrs, mobile);
    SaveConfig(&ptrs);

    web_send_response(c, 200, "OK", "application/json", "{\"status\":\"ok\"}");
}

static void handle_post_reboot(struct web_conn *c){
    // A hardware watchdog reset: whatever loop is currently running (the
    // blocking hotspot session or the main adapter loop) doesn't need to be
    // cooperatively unwound, the chip just resets once the timer fires.
    web_send_response(c, 200, "OK", "application/json", "{\"status\":\"ok\"}");
    // watchdog_reboot(0, 0, WEB_REBOOT_DELAY_MS);
    watchdog_enable(WEB_REBOOT_DELAY_MS, 0);
    watchdog_update();
    while(1);
}

static void web_dispatch(struct web_conn *c, bool is_get, bool is_post, const char *path, const char *body){
    if (is_get && strcmp(path, "/") == 0){
        web_send_response(c, 200, "OK", "text/html", WEB_CONFIG_HTML);
    } else if (is_get && strcmp(path, "/api/config") == 0){
        handle_get_config(c);
    } else if (is_post && strcmp(path, "/api/config") == 0){
        handle_post_config(c, body);
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
    const char *cl = stristr(c->req_buf, "Content-Length:");
    if (cl) content_length = atoi(cl + strlen("Content-Length:"));

    if (is_post){
        int body_have = c->req_len - header_len;
        if (content_length < 0) content_length = 0;
        if (header_len + content_length >= WEB_REQ_BUF_SIZE) content_length = WEB_REQ_BUF_SIZE - header_len - 1;
        if (body_have < content_length) return; // wait for the rest of the body
    }

    char *body = c->req_buf + header_len;
    body[content_length] = '\0';

    web_dispatch(c, is_get, is_post, path, body);
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
    memset(web_conns, 0, sizeof(web_conns));

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
// calling cyw43_arch_poll() for the accept/recv/sent callbacks to fire.
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

    for (int i = 0; i < WEB_MAX_CONNS; i++){
        if (web_conns[i].in_use) web_close_conn(&web_conns[i]);
    }
}

// Used only for the hotspot config fallback, where nothing else is running.
// There is no valid network config to continue with, so this never returns:
// the only way out is the user rebooting via the web page (hardware watchdog).
void web_config_run_blocking(struct mobile_user *mobile){
    if (!web_config_listen(mobile)) return;

    while (true){
        cyw43_arch_poll();
    }
}
