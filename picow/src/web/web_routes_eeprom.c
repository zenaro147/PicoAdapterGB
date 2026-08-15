// /api/eeprom: raw 512-byte EEPROM upload/download for the Mobile Adapter
// GB config blob (MA block: original adapter data, LM block: libmobile data;
// see dependences/libmobile/config.c for the on-disk layout this mirrors).
#include "web_routes.h"

#include <string.h>

#include "storage/flash_eeprom.h"

void handle_get_eeprom(struct web_conn *c){
    struct mobile_user *mobile = web_mobile;
    web_send_raw_response(c, 200, "OK", "application/octet-stream",
        mobile->config_eeprom, sizeof(mobile->config_eeprom));
}

static enum mobile_addrtype web_addrtype_from_byte(uint8_t v){
    switch (v){
        case MOBILE_ADDRTYPE_IPV4: return MOBILE_ADDRTYPE_IPV4;
        case MOBILE_ADDRTYPE_IPV6: return MOBILE_ADDRTYPE_IPV6;
        default: return MOBILE_ADDRTYPE_NONE;
    }
}

static void web_decode_uploaded_addr(struct mobile_addr *addr, uint8_t type_byte, const uint8_t *host, const uint8_t *port_bytes){
    addr->type = web_addrtype_from_byte(type_byte);
    if (addr->type == MOBILE_ADDRTYPE_IPV4){
        struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
        addr4->port = port_bytes[0] | (port_bytes[1] << 8);
        memcpy(addr4->host, host, sizeof(addr4->host));
    } else if (addr->type == MOBILE_ADDRTYPE_IPV6){
        struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
        addr6->port = port_bytes[0] | (port_bytes[1] << 8);
        memcpy(addr6->host, host, sizeof(addr6->host));
    }
}

// Mirrors config_library_load()'s layout (private to libmobile/config.c) so an
// uploaded LM block can be reflected into the live adapter->config, keeping it
// consistent with the raw bytes for a later explicit Save & Reboot.
static void web_apply_uploaded_libmobile_block(struct mobile_user *mobile, const uint8_t *data){
    const uint8_t *lm = data + 0x100;

    // Bit 0x80 of the device byte is MOBILE_CONFIG_DEVICE_UNMETERED (config.h).
    unsigned char device_raw = lm[0x05];
    mobile_config_set_device(mobile->adapter,
        (enum mobile_adapter_device)(device_raw & ~0x80), (device_raw & 0x80) != 0);

    struct mobile_addr dns1 = {.type = MOBILE_ADDRTYPE_NONE};
    struct mobile_addr dns2 = {.type = MOBILE_ADDRTYPE_NONE};
    struct mobile_addr relay = {.type = MOBILE_ADDRTYPE_NONE};
    web_decode_uploaded_addr(&dns1, lm[0x06], lm + 0x20, lm + 0x1a);
    web_decode_uploaded_addr(&dns2, lm[0x07], lm + 0x30, lm + 0x1c);
    web_decode_uploaded_addr(&relay, lm[0x0a], lm + 0x40, lm + 0x1e);
    mobile_config_set_dns(mobile->adapter, &dns1, MOBILE_DNS1);
    mobile_config_set_dns(mobile->adapter, &dns2, MOBILE_DNS2);
    mobile_config_set_relay(mobile->adapter, &relay);

    unsigned p2p_port = lm[0x08] | (lm[0x09] << 8);
    if (p2p_port) mobile_config_set_p2p_port(mobile->adapter, p2p_port);

    mobile_config_set_alt_mail(mobile->adapter, lm[0x0c] != 0);
    mobile_config_set_relay_token(mobile->adapter, lm[0x0b] ? lm + 0x50 : NULL);
}

// Only updates RAM (config_eeprom + the live adapter->config). Nothing is
// written to flash here; the user must press "Save & Reboot" to persist it.
void handle_post_eeprom(struct web_conn *c, const char *body, int content_length){
    struct mobile_user *mobile = web_mobile;

    if (content_length != EEPROM_FILE_SIZE){
        web_send_response(c, 400, "Bad Request", "application/json",
            "{\"status\":\"error\",\"error\":\"EEPROM uploads must be exactly 512 bytes\"}");
        return;
    }

    const char *header = web_stristr(c->req_buf, "X-Eeprom-Replace-Libmobile:");
    bool replace_libmobile = false;
    if (header){
        char value[8] = {0};
        const char *value_p = header + strlen("X-Eeprom-Replace-Libmobile:");
        size_t i = 0;
        while (*value_p && *value_p != '\r' && *value_p != '\n' && i + 1 < sizeof(value)){
            value[i++] = *value_p++;
        }
        value[i] = '\0';
        replace_libmobile = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    }

    const uint8_t *data = (const uint8_t *)body;
    if (data[0] != 'M' || data[1] != 'A'){
        web_send_response(c, 400, "Bad Request", "application/json",
            "{\"status\":\"error\",\"error\":\"Invalid EEPROM: missing MA signature at offset 0x00\"}");
        return;
    }
    if (replace_libmobile && (data[0x100] != 'L' || data[0x101] != 'M')){
        web_send_response(c, 400, "Bad Request", "application/json",
            "{\"status\":\"error\",\"error\":\"Invalid EEPROM: missing LM signature at offset 0x100\"}");
        return;
    }

    if (replace_libmobile){
        memcpy(mobile->config_eeprom, data, sizeof(mobile->config_eeprom));
        web_apply_uploaded_libmobile_block(mobile, data);
    } else {
        if (content_length < EEPROM_ORIGINAL_CONFIG_SIZE) {
            web_send_response(c, 400, "Bad Request", "application/json",
                "{\"status\":\"error\",\"error\":\"Payload too short for the original adapter config\"}");
            return;
        }
        memcpy(mobile->config_eeprom, data, EEPROM_ORIGINAL_CONFIG_SIZE);
    }

    web_send_response(c, 200, "OK", "application/json", "{\"status\":\"ok\"}");
}
