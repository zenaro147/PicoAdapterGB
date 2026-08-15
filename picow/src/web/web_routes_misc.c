// /api/format and /api/reboot: destructive/administrative actions, kept apart
// from the regular config read/write routes.
#include "web_routes.h"

#include <string.h>

#include "hardware/watchdog.h"

#include "storage/flash_eeprom.h"

void handle_post_format(struct web_conn *c){
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

void handle_post_reboot(struct web_conn *c){
    // A hardware watchdog reset: whatever loop is currently running (the
    // blocking hotspot session or the main adapter loop) doesn't need to be
    // cooperatively unwound, the chip just resets once the timer fires.
    web_send_response(c, 200, "OK", "application/json", "{\"status\":\"ok\"}");
    watchdog_enable(WEB_REBOOT_DELAY_MS, 0);
    watchdog_update();
    while(1);
}
