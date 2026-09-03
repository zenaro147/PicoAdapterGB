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

#include "net/socket_hal.h"
#include "net/net_hal.h"
#include "pio/linkcable.h"

bool isLinkCable32 = false;

static volatile bool have_config_to_write = false;
static user_time_t time_last_config_edit = 0;

// Device-auth side channel: mobile_func_update_device_auth (mobile.h) fires
// synchronously from inside mobile_loop() and "must not block, as it's
// called synchronously from within the library" (its own doc comment) - so
// impl_update_device_auth() below only ever captures the request into this
// small queue. The actual DNS resolve + HTTP GET (net_device_auth_*(), see
// net/net_hal.h) is driven from adapter_bridge_service_device_auth(), called
// once per main-loop iteration from main.c, exactly like every other
// deferred-work pattern in this codebase (config flash save, web lifecycle).
//
// This is a best-effort side channel: the server holds its own TTL and a
// manual resync flow for a missed/failed authorize or deauthorize call (see
// the device-auth design notes), so a full queue or a failed resolve/request
// here just drops that one request rather than blocking or retrying forever.
// This address is not user- or build-configurable by design - not via a
// runtime setting, not via a compile-time override. Test setups needing a
// different target must do so outside this source (e.g. a hosts-file-level
// DNS override on the test network, or a separate build never shipped).
#define DEVICE_AUTH_HOSTNAME "device.auth.dion.ne.jp"
#define DEVICE_AUTH_PORT 80
#define DEVICE_AUTH_QUEUE_SIZE 4

struct device_auth_request {
    enum mobile_device_auth_action action;
    unsigned char ppp_id[0x20];
    unsigned ppp_id_size;
    uint64_t counter;
    unsigned char sig[MOBILE_DEVICE_AUTH_SIG_SIZE];
};

static struct device_auth_request device_auth_queue[DEVICE_AUTH_QUEUE_SIZE];
static unsigned device_auth_queue_head = 0;
static unsigned device_auth_queue_count = 0;

enum device_auth_service_phase {
    DEVICE_AUTH_SERVICE_IDLE,
    DEVICE_AUTH_SERVICE_RESOLVING,
    DEVICE_AUTH_SERVICE_REQUESTING,
};
static enum device_auth_service_phase device_auth_service_phase = DEVICE_AUTH_SERVICE_IDLE;

// Resolved once and reused for every subsequent authorize/deauthorize call,
// instead of re-resolving DEVICE_AUTH_HOSTNAME on every single request - the
// server's address isn't expected to change mid-session. Invalidated (see
// DEVICE_AUTH_SERVICE_REQUESTING below) only on a transport-level failure,
// on the theory that a stale/wrong cached IP is a more likely explanation
// than a one-off network blip, so it's worth a fresh resolve before the next
// attempt; an HTTP-level response (even an error status) leaves it alone,
// since that proves the cached IP was reachable.
static bool device_auth_ip_known = false;
static unsigned char device_auth_ip[4];

static void impl_debug_log(void *user, const char *line){
    (void)user;
    printf("%s\n", line);
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
    memcpy(dest, mobile->config_eeprom + offset, size);
    return true;
}

static bool impl_config_write(void *user, const void *src, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    bool this_edited_config = memcmp(mobile->config_eeprom + offset, src, size) != 0;
    if (this_edited_config) {
        memcpy(mobile->config_eeprom + offset, src, size);
        LED_ON;
        have_config_to_write = true;
        time_last_config_edit = TIME_FUNCTION;
    }
    return true;
}

// Same as impl_config_write(), minus the pending-write/LED signaling: used
// only by the web UI's config snapshot, whose edits must stay invisible to
// main()'s "flush the live adapter's config to flash" loop until an explicit
// Save & Reboot copies the snapshot's bytes over.
static bool impl_config_write_snapshot(void *user, const void *src, const uintptr_t offset, const size_t size) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    memcpy(mobile->config_eeprom + offset, src, size);
    return true;
}

static void impl_time_latch(void *user, unsigned timer) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    mobile->clock_latch[timer] = time_us_64();
}

static bool impl_time_check_ms(void *user, unsigned timer, unsigned ms) {
    struct mobile_user *mobile = (struct mobile_user *)user;
    return ((time_us_64() - mobile->clock_latch[timer]) >= MS(ms));
}

static bool impl_sock_open(void *user, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){
    struct mobile_user *mobile = (struct mobile_user *)user;
    return socket_impl_open(mobile->socket[conn], socktype, addrtype, bindport, user);
}

static void impl_sock_close(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    socket_impl_close(mobile->socket[conn]);
}

static int impl_sock_connect(void *user, unsigned conn, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    return socket_impl_connect(mobile->socket[conn], addr);
}

static int impl_sock_send(void *user, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    return socket_impl_send(mobile->socket[conn], data, size, addr);
}

static int impl_sock_recv(void *user, unsigned conn, void *data, unsigned size, struct mobile_addr *addr){
    struct mobile_user *mobile = (struct mobile_user *)user;
    return socket_impl_recv(mobile->socket[conn], data, size, addr);
}

static bool impl_sock_listen(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    return socket_impl_listen(mobile->socket[conn], user);
}

static bool impl_sock_accept(void *user, unsigned conn){
    struct mobile_user *mobile = (struct mobile_user *)user;
    return socket_impl_accept(mobile->socket[conn]);
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

static void impl_update_device_auth(void *user, enum mobile_device_auth_action action, const unsigned char *ppp_id, unsigned ppp_id_size, uint64_t counter, const unsigned char *sig){
    (void)user;
    if (device_auth_queue_count >= DEVICE_AUTH_QUEUE_SIZE) {
        DEBUG_PRINT_FUNCTION("Device-auth: queue full, dropping request.");
        return;
    }

    unsigned tail = (device_auth_queue_head + device_auth_queue_count) % DEVICE_AUTH_QUEUE_SIZE;
    struct device_auth_request *req = &device_auth_queue[tail];
    req->action = action;
    req->ppp_id_size = ppp_id_size < sizeof(req->ppp_id) ? ppp_id_size : sizeof(req->ppp_id);
    memcpy(req->ppp_id, ppp_id, req->ppp_id_size);
    req->counter = counter;
    memcpy(req->sig, sig, sizeof(req->sig));
    device_auth_queue_count++;
}

static void device_auth_pop_and_reset(void){
    device_auth_queue_head = (device_auth_queue_head + 1) % DEVICE_AUTH_QUEUE_SIZE;
    device_auth_queue_count--;
    device_auth_service_phase = DEVICE_AUTH_SERVICE_IDLE;
}

static void device_auth_start_http_request(const unsigned char ip[4]){
    struct device_auth_request *req = &device_auth_queue[device_auth_queue_head];

    char ppp_id_str[sizeof(req->ppp_id) + 1];
    memcpy(ppp_id_str, req->ppp_id, req->ppp_id_size);
    ppp_id_str[req->ppp_id_size] = '\0';

    static const char hex_digits[] = "0123456789abcdef";
    char sig_hex[sizeof(req->sig) * 2 + 1];
    for (unsigned i = 0; i < sizeof(req->sig); i++) {
        sig_hex[i * 2] = hex_digits[req->sig[i] >> 4];
        sig_hex[i * 2 + 1] = hex_digits[req->sig[i] & 0x0F];
    }
    sig_hex[sizeof(sig_hex) - 1] = '\0';

    char request_line[256];
    snprintf(request_line, sizeof(request_line),
        "GET /api/adapter/device-auth?ppp_id=%s&action=%s&counter=%llu&sig=%s HTTP/1.0",
        ppp_id_str,
        req->action == MOBILE_DEVICE_AUTH_AUTHORIZE ? "authorize" : "deauthorize",
        (unsigned long long)req->counter, sig_hex);

    net_device_auth_http_get_start(ip, DEVICE_AUTH_PORT, request_line);
    device_auth_service_phase = DEVICE_AUTH_SERVICE_REQUESTING;
}

void adapter_bridge_service_device_auth(struct mobile_user *mobile){
    switch (device_auth_service_phase) {
        case DEVICE_AUTH_SERVICE_IDLE: {
            if (device_auth_queue_count == 0) return;

            if (device_auth_ip_known) {
                device_auth_start_http_request(device_auth_ip);
                return;
            }

            struct mobile_addr dns1, dns2;
            mobile_config_get_dns(mobile->adapter, &dns1, MOBILE_DNS1);
            mobile_config_get_dns(mobile->adapter, &dns2, MOBILE_DNS2);
            net_device_auth_resolve_start(DEVICE_AUTH_HOSTNAME, &dns1, &dns2);
            device_auth_service_phase = DEVICE_AUTH_SERVICE_RESOLVING;
            return;
        }
        case DEVICE_AUTH_SERVICE_RESOLVING: {
            if (!net_device_auth_resolve_done()) return;

            if (!net_device_auth_resolve_result(device_auth_ip)) {
                DEBUG_PRINT_FUNCTION("Device-auth: failed to resolve %s, dropping request.", DEVICE_AUTH_HOSTNAME);
                device_auth_pop_and_reset();
                return;
            }

            device_auth_ip_known = true;
            device_auth_start_http_request(device_auth_ip);
            return;
        }
        case DEVICE_AUTH_SERVICE_REQUESTING: {
            if (!net_device_auth_http_get_done()) return;
            int status = net_device_auth_http_get_result();
            DEBUG_PRINT_FUNCTION("Device-auth: request finished, status %d.", status);
            if (status < 0) {
                // Transport-level failure (connect/timeout/malformed
                // response), not an HTTP-level rejection - the cached IP may
                // be stale or wrong, so force a fresh resolve next time
                // rather than repeatedly hammering a bad address.
                device_auth_ip_known = false;
            }
            device_auth_pop_and_reset();
            return;
        }
    }
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
    mobile_def_update_device_auth(adapter, impl_update_device_auth);
}

void adapter_bridge_register_snapshot_callbacks(struct mobile_adapter *adapter){
    mobile_def_debug_log(adapter, impl_debug_log);
    mobile_def_config_read(adapter, impl_config_read);
    mobile_def_config_write(adapter, impl_config_write_snapshot);
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
