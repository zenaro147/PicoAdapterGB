// libmobile <-> firmware glue: implements every mobile_impl_*() callback and
// registers them on the adapter. This is the only place that should touch
// mobile->config_eeprom, the per-socket state array, and the link-cable mode
// flag on behalf of libmobile.
#include "adapter_bridge.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "pico/stdlib.h"
#include "pico/unique_id.h"

#include <mobile_inet.h>

#include "net/socket_hal.h"
#include "net/net_hal.h"
#include "pio/linkcable.h"

bool isLinkCable32 = false;

static volatile bool have_config_to_write = false;
static user_time_t time_last_config_edit = 0;

// Device-auth side channel: mobile_func_update_device_auth (mobile.h) is a
// plain synchronous call from within mobile_loop() (during
// mobile_actions_process()) and "must not block" (its own doc comment) - so
// impl_update_device_auth() below only ever captures the request into this
// small queue. The actual HTTP GET (net_device_auth_http_get_*(), see
// net/net_hal.h) is driven from adapter_bridge_service_device_auth(), called
// once per main-loop iteration from main.c, exactly like every other
// deferred-work pattern in this codebase (config flash save, web lifecycle).
// libmobile itself now resolves the server's address internally (through
// the session's own configured DNS1/DNS2) before this callback ever fires,
// so the frontend never needs to know the hostname or do its own DNS lookup
// - the resolved address arrives as one of the callback's parameters.
//
// This is a best-effort side channel: the server holds its own TTL and a
// manual resync flow for a missed/failed authorize or deauthorize call (see
// the device-auth design notes), so a full queue or a failed request here
// just drops that one request rather than blocking or retrying forever.
#define DEVICE_AUTH_PORT 80
#define DEVICE_AUTH_QUEUE_SIZE 4

struct device_auth_request {
    enum mobile_device_auth_action action;
    unsigned char ppp_id[0x20];
    unsigned ppp_id_size;
    uint64_t counter;
    unsigned char sig[MOBILE_DEVICE_AUTH_SIG_SIZE];
    unsigned char addr_ipv4[4];
    // Empty string when libmobile passed NULL - it signs the older form of
    // the message in that case, and the request must then omit device=
    // entirely rather than send it blank.
    char device[MOBILE_DEVICE_ID_STR_SIZE];
    // A counter query rather than an authorize/deauthorize: same server, same
    // shape of request, but it carries no counter and its answer has to be
    // handed back to libmobile instead of merely logged.
    bool is_query;
};

static struct device_auth_request device_auth_queue[DEVICE_AUTH_QUEUE_SIZE];
static unsigned device_auth_queue_head = 0;
static unsigned device_auth_queue_count = 0;

enum device_auth_service_phase {
    DEVICE_AUTH_SERVICE_IDLE,
    DEVICE_AUTH_SERVICE_REQUESTING,
    // Same HTTP client, but the answer's body has to reach libmobile instead
    // of just being logged, so the finish step differs.
    DEVICE_AUTH_SERVICE_QUERYING,
};
static enum device_auth_service_phase device_auth_service_phase = DEVICE_AUTH_SERVICE_IDLE;

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

static void impl_update_device_auth(void *user, enum mobile_device_auth_action action, const unsigned char *ppp_id, unsigned ppp_id_size, uint64_t counter, const unsigned char *sig, const unsigned char *addr_ipv4, const char *device){
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
    memcpy(req->addr_ipv4, addr_ipv4, sizeof(req->addr_ipv4));
    if (device) {
        strncpy(req->device, device, sizeof(req->device) - 1);
        req->device[sizeof(req->device) - 1] = '\0';
    } else {
        req->device[0] = '\0';
    }
    req->is_query = false;
    device_auth_queue_count++;
}

// Counter query: libmobile fires this once per session, before any
// authorization event, to find out where the server thinks our counter
// stands (see mobile.h). Queued through the same path as the authorize
// requests so both share one HTTP client and can't overlap on it.
static bool impl_device_auth_query(void *user, const unsigned char *addr_ipv4, const unsigned char *ppp_id, unsigned ppp_id_size, uint64_t counter, const unsigned char *sig, const char *device){
    (void)user;
    if (device_auth_queue_count >= DEVICE_AUTH_QUEUE_SIZE) {
        DEBUG_PRINT_FUNCTION("Device-auth: queue full, dropping query.");
        return false;
    }

    unsigned tail = (device_auth_queue_head + device_auth_queue_count) % DEVICE_AUTH_QUEUE_SIZE;
    struct device_auth_request *req = &device_auth_queue[tail];
    req->action = MOBILE_DEVICE_AUTH_AUTHORIZE; // unused for a query
    req->ppp_id_size = ppp_id_size < sizeof(req->ppp_id) ? ppp_id_size : sizeof(req->ppp_id);
    memcpy(req->ppp_id, ppp_id, req->ppp_id_size);
    // The query consumes a counter value of its own: it goes out as
    // &counter= and into the signature, and the server echoes it back so
    // the answer is bound to this exact request. A replayed older answer
    // carries a different echo and the core rejects it.
    req->counter = counter;
    memcpy(req->sig, sig, sizeof(req->sig));
    memcpy(req->addr_ipv4, addr_ipv4, sizeof(req->addr_ipv4));
    if (device) {
        strncpy(req->device, device, sizeof(req->device) - 1);
        req->device[sizeof(req->device) - 1] = '\0';
    } else {
        req->device[0] = '\0';
    }
    req->is_query = true;
    device_auth_queue_count++;
    return true;
}

// The board's own unique id, which libmobile hashes into the device id sent
// as device=. Deliberately not a Wi-Fi MAC: on the esp backend the radio is a
// separate, replaceable module, so a MAC would follow the module rather than
// the board that actually holds the device-auth key and counter - swapping it
// would look like a brand new device and burn one of the account's slots.
// This id is also readable before any network exists, and needs no storage of
// its own (mobile.h warns against keeping an id in the config blob, since
// that blob is legitimately copied between devices).
static unsigned impl_device_identity(void *user, void *data, unsigned size){
    (void)user;
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    if (size < sizeof(board_id.id)) return 0;
    memcpy(data, board_id.id, sizeof(board_id.id));
    return sizeof(board_id.id);
}

static void device_auth_pop_and_reset(void){
    device_auth_queue_head = (device_auth_queue_head + 1) % DEVICE_AUTH_QUEUE_SIZE;
    device_auth_queue_count--;
    device_auth_service_phase = DEVICE_AUTH_SERVICE_IDLE;
}

void adapter_bridge_service_device_auth(struct mobile_user *mobile){
    switch (device_auth_service_phase) {
        case DEVICE_AUTH_SERVICE_IDLE: {
            if (device_auth_queue_count == 0) return;
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

            // device= is omitted entirely when libmobile had no id to give:
            // it signed the legacy form of the message in that case, so
            // sending an empty parameter would break signature verification.
            char device_param[sizeof(req->device) + 8];
            if (req->device[0] != '\0') {
                snprintf(device_param, sizeof(device_param), "&device=%s", req->device);
            } else {
                device_param[0] = '\0';
            }

            char request_line[256];
            if (req->is_query) {
                snprintf(request_line, sizeof(request_line),
                    "GET /api/adapter/device-auth?ppp_id=%s%s&action=query&counter=%llu&sig=%s HTTP/1.0",
                    ppp_id_str, device_param, (unsigned long long)req->counter, sig_hex);
            } else {
                snprintf(request_line, sizeof(request_line),
                    "GET /api/adapter/device-auth?ppp_id=%s%s&action=%s&counter=%llu&sig=%s HTTP/1.0",
                    ppp_id_str, device_param,
                    req->action == MOBILE_DEVICE_AUTH_AUTHORIZE ? "authorize" : "deauthorize",
                    (unsigned long long)req->counter, sig_hex);
            }

            net_device_auth_http_get_start(req->addr_ipv4, DEVICE_AUTH_PORT, request_line);
            device_auth_service_phase = req->is_query ?
                DEVICE_AUTH_SERVICE_QUERYING : DEVICE_AUTH_SERVICE_REQUESTING;
            return;
        }
        case DEVICE_AUTH_SERVICE_REQUESTING: {
            if (!net_device_auth_http_get_done()) return;
            int status = net_device_auth_http_get_result();
            DEBUG_PRINT_FUNCTION("Device-auth: request finished, status %d.", status);
            device_auth_pop_and_reset();
            return;
        }
        case DEVICE_AUTH_SERVICE_QUERYING: {
            if (!net_device_auth_http_get_done()) return;
            int status = net_device_auth_http_get_result();
            unsigned body_len = 0;
            const char *body = net_device_auth_http_get_body(&body_len);
            DEBUG_PRINT_FUNCTION("Device-auth: query finished, status %d, %u body bytes.",
                status, body_len);
            // Handed over exactly as received. libmobile verifies the
            // response signature, parses strictly and refuses to move the
            // counter backwards - so this must not pre-validate, trim or
            // reinterpret anything. Anything other than a 200 is reported as
            // a failure (NULL), since only a 200 carries an answer at all.
            if (status == 200) {
                mobile_device_auth_query_result(mobile->adapter, body, body_len);
            } else {
                mobile_device_auth_query_result(mobile->adapter, NULL, 0);
            }
            // The one moment the block state can change is right here, once
            // the core has verified the answer - so this is where it's
            // reported, and only on a verified YES. UNKNOWN (no answer, bad
            // transport, unverifiable body) deliberately prints nothing: it
            // behaves as not-blocked, and saying so would turn a dropped
            // packet into a false reassurance. Not shown on the web UI
            // because it structurally can't be: the state never persists and
            // only exists after a query inside a PPP session, and the web
            // server is shut down the moment a session starts.
            if (mobile_device_auth_block_state(mobile->adapter) == MOBILE_DEVICE_AUTH_BLOCK_YES) {
                DEBUG_PRINT_FUNCTION("Device-auth: this adapter is BLOCKED on the site; network for this session is disabled.");
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
    // The name is an input to the device-id hash, so it is written out as a
    // literal here on purpose and must never be swapped for PICO_ADAPTER_HARDWARE,
    // a CMake-supplied define, or anything else a rename could reach: changing
    // the spelling silently turns every adapter in the field into a new device
    // on the server. See mobile.h's list of names in use.
    mobile_def_device_identity(adapter, impl_device_identity, "picoadaptergb");
    mobile_def_device_auth_query(adapter, impl_device_auth_query);
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
