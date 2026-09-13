// SPDX-License-Identifier: GPL-3.0-only
// /api/config: reads and writes the libmobile-facing settings (Wi-Fi, DNS,
// relay, device, etc.). Reachable only while the setup web server is alive.
#include "web_routes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "storage/flash_eeprom.h"

static void handle_get_config_impl(struct web_conn *c){
    // Everything editable comes from the snapshot, so a page reload always
    // shows what the web UI last wrote (including still-unsaved edits) -
    // never the live adapter's in-progress state.
    struct mobile_user *mobile = web_mobile_snapshot;
    // The relay-assigned number is the one exception: only a live relay
    // session can ever obtain one, so it's read from the live adapter.
    struct mobile_user *live = web_mobile;

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
    web_format_addr_ip_only(&dns1, dns1str, sizeof(dns1str));
    web_format_addr_ip_only(&dns2, dns2str, sizeof(dns2str));
    web_format_addr_ip_only(&relay, relaystr, sizeof(relaystr));

    int dns_port = MOBILE_DNS_PORT;
    if (dns1.type == MOBILE_ADDRTYPE_IPV4) dns_port = dns1._addr4.port;
    else if (dns2.type == MOBILE_ADDRTYPE_IPV4) dns_port = dns2._addr4.port;

    char token_hex[MOBILE_RELAY_TOKEN_SIZE * 2 + 1] = {0};

    bool token_valid = false;

    for (int i = 0; i < MOBILE_RELAY_TOKEN_SIZE; i++){
        if (token[i] != 0){
            token_valid = true;
            break;
        }
    }

    if (token_valid){
        for (int i = 0; i < MOBILE_RELAY_TOKEN_SIZE; i++)
            sprintf(token_hex + i * 2, "%02hhX", token[i]);
    }

    const char *device_str = "BLUE";
    switch (device){
        case MOBILE_ADAPTER_YELLOW: device_str = "YELLOW"; break;
        case MOBILE_ADAPTER_GREEN:  device_str = "GREEN";  break;
        case MOBILE_ADAPTER_RED:    device_str = "RED";    break;
        default:                    device_str = "BLUE";   break;
    }

    char wifi_ssid_esc[SSID_LENGHT] = {0};
    char wifi_pass_esc[PASS_LENGHT] = {0};
    web_json_escape(mobile->wifiSSID, wifi_ssid_esc, sizeof(wifi_ssid_esc));
    web_json_escape(mobile->wifiPASS, wifi_pass_esc, sizeof(wifi_pass_esc));

    char relay_number_esc[MOBILE_MAX_NUMBER_SIZE + 1] = {0};
    web_json_escape(live->number_user, relay_number_esc, sizeof(relay_number_esc));

    // Also from the live adapter, and for the same kind of reason: the
    // pairing code identifies this board, so it is never one of the user's
    // editable settings and must not come from the snapshot. Empty when this
    // build has no device identity - the page then hides the row rather than
    // showing a blank code. Note this is unrelated to the "device" field
    // below, which is the Mobile Adapter's colour/type.
    char pairing_code[MOBILE_PAIRING_CODE_STR_SIZE];
    if (!mobile_device_auth_get_pairing_code(live->adapter, pairing_code)) {
        pairing_code[0] = '\0';
    }

    // The pairing code above comes from device identity alone, not from
    // whether a mail key is provisioned - a board can show a perfectly
    // normal pairing code and still have no device_auth_key, since the two
    // are independent. Surfaced separately so the page can tell the two
    // apart instead of a working-looking pairing code implying mail is
    // ready too. Only the presence of a key matters here, never its value -
    // the buffer is cleared immediately after the call so the secret
    // doesn't linger on the stack any longer than the getter requires.
    // Cleared through a volatile pointer rather than plain memset(): a
    // buffer nothing reads afterward is exactly the dead store an optimizer
    // is entitled to remove, and explicit_bzero()/memset_s() aren't
    // available on this toolchain.
    unsigned char device_auth_key[MOBILE_DEVICE_AUTH_KEY_SIZE];
    bool mail_key_provisioned = mobile_config_get_device_auth_key(live->adapter, device_auth_key);
    volatile unsigned char *device_auth_key_wipe = device_auth_key;
    for (size_t i = 0; i < sizeof(device_auth_key); i++) device_auth_key_wipe[i] = 0;

    // 832 + room for the mail_key_provisioned field (26 bytes) and its margin.
    char body[880];
    snprintf(body, sizeof(body),
        "{"
        "\"wifi_ssid\":\"%s\","
        "\"wifi_pass\":\"%s\","
        "\"dns1\":\"%s\","
        "\"dns2\":\"%s\","
        "\"dns_port\":%d,"
        "\"relay\":\"%s\","
        "\"relay_token\":\"%s\","
        "\"relay_number\":\"%s\","
        "\"p2p_port\":%u,"
        "\"device\":\"%s\","
        "\"unmetered\":%s,"
        "\"redirect_mail\":%s,"
        "\"libmobile_version\":\"%u.%u.%u\","
        "\"firmware_version\":\"%s\","
        "\"pairing_code\":\"%s\","
        "\"mail_key_provisioned\":%s"
        "}",
        wifi_ssid_esc, wifi_pass_esc, dns1str, dns2str, dns_port, relaystr, token_hex, relay_number_esc, p2p_port,
        device_str, unmetered ? "true" : "false", redirect_mail ? "true" : "false",
        mobile_version_major, mobile_version_minor, mobile_version_patch,
        PICO_ADAPTER_SOFTWARE, pairing_code, mail_key_provisioned ? "true" : "false");

    web_send_response(c, 200, "OK", "application/json", body);
}

void handle_get_config(struct web_conn *c){
    handle_get_config_impl(c);
}

void handle_get_relay_number(struct web_conn *c){
    struct mobile_user *mobile = web_mobile;

    struct mobile_addr relay = {.type = MOBILE_ADDRTYPE_NONE};
    mobile_config_get_relay(mobile->adapter, &relay);

    char relaystr[60] = {0};
    web_format_addr_ip_only(&relay, relaystr, sizeof(relaystr));

    char relay_number_esc[MOBILE_MAX_NUMBER_SIZE + 1] = {0};
    web_json_escape(mobile->number_user, relay_number_esc, sizeof(relay_number_esc));

    char body[128];
    snprintf(body, sizeof(body),
        "{\"relay\":\"%s\",\"relay_number\":\"%s\"}",
        relaystr, relay_number_esc);

    web_send_response(c, 200, "OK", "application/json", body);
}

void handle_post_config(struct web_conn *c, const char *body){
    // Edits go to the snapshot only; the live adapter never sees them until
    // Save & Reboot persists the snapshot to flash and the device reboots.
    struct mobile_user *mobile = web_mobile_snapshot;
    char field[128];
    bool needSave = false;

    DEBUG_PRINT_FUNCTION("Web config POST received.");

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
        if (field[0] && web_parse_addr(&dns1, field)) web_set_addr_port(&dns1, dns_port);
        mobile_config_set_dns(mobile->adapter, &dns1, MOBILE_DNS1);
        needSave = true;
    }
    if (web_form_get(body, "dns2", field, sizeof(field))){
        struct mobile_addr dns2 = {.type = MOBILE_ADDRTYPE_NONE};
        if (field[0] && web_parse_addr(&dns2, field)) web_set_addr_port(&dns2, dns_port);
        mobile_config_set_dns(mobile->adapter, &dns2, MOBILE_DNS2);
        needSave = true;
    }
    if (web_form_get(body, "relay", field, sizeof(field))){
        struct mobile_addr relay = {.type = MOBILE_ADDRTYPE_NONE};
        if (field[0] && web_parse_addr(&relay, field)) web_set_addr_port(&relay, MOBILE_DEFAULT_RELAY_PORT);
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
    }

    web_send_response(c, 200, "OK", "application/json", "{\"status\":\"ok\"}");
    if (needSave) {
        DEBUG_PRINT_FUNCTION("Web config POST accepted; scheduling flash save.");
        web_config_request_save_reboot();
    } else {
        DEBUG_PRINT_FUNCTION("Web config POST contained no changes.");
    }
}
