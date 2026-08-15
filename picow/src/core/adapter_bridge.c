// libmobile <-> firmware glue: implements every mobile_impl_*() callback and
// registers them on the adapter. This is the only place that should touch
// mobile->config_eeprom, the per-socket state array, and the link-cable mode
// flag on behalf of libmobile.
#include "adapter_bridge.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "pico/stdlib.h"

#include <mobile_inet.h>

#include "net/picow/socket_impl.h"
#include "pio/linkcable.h"

bool isLinkCable32 = false;

static volatile bool have_config_to_write = false;
static user_time_t time_last_config_edit = 0;

static void impl_debug_log(void *user, const char *line){
    (void)user;
    fprintf(stderr, "%s\n", line);
}

static void impl_serial_disable(void *user) {
    (void)user;
    linkcable_reset(false);
}

static void impl_serial_enable(void *user, bool mode_32bit) {
    (void)user;
    isLinkCable32 = mode_32bit;
    linkcable_set_is_32(mode_32bit);
    linkcable_enable();
}

static bool impl_config_read(void *user, void *dest, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    for (size_t i = 0; i < size; i++){
        ((char *)dest)[i] = (char)mobile->config_eeprom[offset + i];
    }
    return true;
}

static bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    const uint8_t *src_8 = (const uint8_t *)src;
    bool this_edited_config = false;
    for (size_t i = 0; i < size; i++) {
        if (mobile->config_eeprom[offset + i] != src_8[i])
            this_edited_config = true;
        mobile->config_eeprom[offset + i] = src_8[i];
    }
    if (this_edited_config) {
        LED_ON;
        have_config_to_write = true;
        time_last_config_edit = TIME_FUNCTION;
    }
    return true;
}

static void impl_time_latch(void *user, unsigned timer) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->picow_clock_latch[timer] = time_us_64();
}

static bool impl_time_check_ms(void *user, unsigned timer, unsigned ms) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    return ((time_us_64() - mobile->picow_clock_latch[timer]) >= MS(ms));
}

static bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->currentReqSocket = conn;
    return socket_impl_open(&mobile->socket[conn], socktype, addrtype, bindport, user);
}

static void impl_sock_close(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->currentReqSocket = conn;
    socket_impl_close(&mobile->socket[conn]);
}

static int impl_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->currentReqSocket = conn;
    return socket_impl_connect(&mobile->socket[conn], addr);
}

static int impl_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->currentReqSocket = conn;
    return socket_impl_send(&mobile->socket[conn], data, size, addr);
}

static int impl_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->currentReqSocket = conn;
    return socket_impl_recv(&mobile->socket[conn], data, size, addr);
}

static bool impl_sock_listen(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->currentReqSocket = conn;
    return socket_impl_listen(&mobile->socket[conn], user);
}

static bool impl_sock_accept(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->currentReqSocket = conn;
    return socket_impl_accept(&mobile->socket[conn]);
}

static void impl_update_number(void *user, enum mobile_number type, const char *number){
    struct mobile_user *mobile = (struct mobile_user *)user;
    char *dest = NULL;

    switch (type) {
        case MOBILE_NUMBER_USER: dest = mobile->number_user; break;
        case MOBILE_NUMBER_PEER: dest = mobile->number_peer; break;
        default: assert(false); return;
    }

    if (number) {
        strncpy(dest, number, MOBILE_MAX_NUMBER_SIZE);
        dest[MOBILE_MAX_NUMBER_SIZE] = '\0';
    } else {
        dest[0] = '\0';
    }

    LED_OFF;
}

void adapter_bridge_register_callbacks(struct mobile_adapter *adapter){
    mobile_def_debug_log(adapter, impl_debug_log);
    mobile_def_serial_disable(adapter, impl_serial_disable);
    mobile_def_serial_enable(adapter, impl_serial_enable);
    mobile_def_config_read(adapter, impl_config_read);
    mobile_def_config_write(adapter, impl_config_write);
    mobile_def_time_latch(adapter, impl_time_latch);
    mobile_def_time_check_ms(adapter, impl_time_check_ms);
    mobile_def_sock_open(adapter, impl_sock_open);
    mobile_def_sock_close(adapter, impl_sock_close);
    mobile_def_sock_connect(adapter, impl_sock_connect);
    mobile_def_sock_listen(adapter, impl_sock_listen);
    mobile_def_sock_accept(adapter, impl_sock_accept);
    mobile_def_sock_send(adapter, impl_sock_send);
    mobile_def_sock_recv(adapter, impl_sock_recv);
    mobile_def_update_number(adapter, impl_update_number);
}

bool adapter_bridge_has_pending_config_write(void){
    return have_config_to_write;
}

void adapter_bridge_clear_pending_config_write(void){
    have_config_to_write = false;
}

user_time_t adapter_bridge_last_config_edit_time(void){
    return time_last_config_edit;
}
