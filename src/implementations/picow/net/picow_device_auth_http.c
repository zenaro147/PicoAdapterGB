// Standalone, non-blocking one-shot HTTP GET implementing net_hal.h's
// net_device_auth_http_get_*() for this backend. Used only for the
// device-auth side channel (see core/adapter_bridge.c) - never seen by
// libmobile, and never sharing lwIP state with socket_impl.c's sockets: this
// owns its own dedicated tcp_pcb, so it can never contend with or corrupt a
// real Mobile Adapter connection. May run sequentially after
// picow_device_auth_dns.c's resolver (never concurrently with it - see
// net_hal.h), but is otherwise fully independent of it.
#include "net/net_hal.h"

#include <string.h>
#include <stdio.h>

#include "lwip/tcp.h"
#include "lwip/ip_addr.h"

#include "pico/time.h"
#include "pico/cyw43_arch.h"

#define DEVICE_AUTH_HTTP_TIMEOUT_MS 5000
#define DEVICE_AUTH_HTTP_REQUEST_MAX 512
#define DEVICE_AUTH_HTTP_RESP_MAX 128

enum device_auth_http_phase {
    DA_HTTP_IDLE,
    DA_HTTP_CONNECTING,
    DA_HTTP_RECEIVING,  // still looking for a full "HTTP/1.x SSS" line
    DA_HTTP_DRAINING,   // status line parsed; discarding the rest until the peer closes
    DA_HTTP_FINISHED,
};

struct device_auth_http_state {
    enum device_auth_http_phase phase;
    int result; // -1 on failure, else the parsed HTTP status code

    struct tcp_pcb *pcb;

    char request[DEVICE_AUTH_HTTP_REQUEST_MAX];
    unsigned request_len;

    char resp[DEVICE_AUTH_HTTP_RESP_MAX];
    unsigned resp_len;

    uint64_t started_at_us;
};

static struct device_auth_http_state da_http;

static void device_auth_http_close_pcb(void){
    if (da_http.pcb){
        // Same reasoning as socket_impl.c's socket_impl_close(): don't clear
        // tcp_arg/tcp_recv/tcp_err before tcp_close(), since lwIP can still
        // invoke them internally (FIN_WAIT/CLOSING/TIME_WAIT) after this
        // returns, once da_http.pcb itself has already gone back to NULL.
        err_t err = tcp_close(da_http.pcb);
        if (err != ERR_OK) tcp_abort(da_http.pcb);
        da_http.pcb = NULL;
    }
}

static void device_auth_http_finish(int result){
    device_auth_http_close_pcb();
    da_http.phase = DA_HTTP_FINISHED;
    da_http.result = result;
}

// Parses "HTTP/1.x SSS <reason>" - scans for the first space rather than
// assuming a fixed offset, so it doesn't care whether the server said
// HTTP/1.0 or HTTP/1.1.
static int parse_status_line(const char *buf, unsigned len){
    unsigned i = 0;
    while (i < len && buf[i] != ' ') i++;
    if (i >= len) return -1;
    i++;
    if (i + 3 > len) return -1;
    if (buf[i] < '0' || buf[i] > '9' ||
        buf[i + 1] < '0' || buf[i + 1] > '9' ||
        buf[i + 2] < '0' || buf[i + 2] > '9') return -1;
    return (buf[i] - '0') * 100 + (buf[i + 1] - '0') * 10 + (buf[i + 2] - '0');
}

static err_t on_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err){
    (void)arg; (void)err;

    if (!p){
        // Remote closed - the normal end of a Connection: close response.
        // Finish now with whatever result we already have (parsed while
        // draining, or parse for the first time if the status line and the
        // close arrived in the same read).
        if (da_http.phase == DA_HTTP_RECEIVING){
            device_auth_http_finish(parse_status_line(da_http.resp, da_http.resp_len));
        } else if (da_http.phase == DA_HTTP_DRAINING){
            device_auth_http_finish(da_http.result);
        }
        return ERR_OK;
    }

    if (da_http.phase != DA_HTTP_RECEIVING && da_http.phase != DA_HTTP_DRAINING){
        tcp_recved(tpcb, p->tot_len);
        pbuf_free(p);
        return ERR_OK;
    }

    if (da_http.phase == DA_HTTP_RECEIVING){
        unsigned space = sizeof(da_http.resp) - da_http.resp_len;
        unsigned copy_len = p->tot_len < space ? p->tot_len : space;
        if (copy_len > 0){
            pbuf_copy_partial(p, da_http.resp + da_http.resp_len, copy_len, 0);
            da_http.resp_len += copy_len;
        }
    }
    // DA_HTTP_DRAINING: don't touch da_http.resp anymore, just keep acking
    // below so the peer can flush whatever it still has queued and close
    // on its own.

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    if (da_http.phase == DA_HTTP_RECEIVING){
        for (unsigned i = 0; i + 1 < da_http.resp_len; i++){
            if (da_http.resp[i] == '\r' && da_http.resp[i + 1] == '\n'){
                // Status line found - deliberately NOT closing yet. Closing
                // the pcb here while the server may still be sending the
                // rest of the response causes it to see the connection torn
                // down mid-response - a real bug found on this exact
                // endpoint by a sibling frontend (logged there as spurious
                // 499/RST server-side). Record the result and wait for the
                // server's own close instead.
                da_http.result = parse_status_line(da_http.resp, i);
                da_http.phase = DA_HTTP_DRAINING;
                return ERR_OK;
            }
        }
        if (da_http.resp_len >= sizeof(da_http.resp)){
            // Status line never terminated within our small buffer. We
            // don't know the code, but still don't close early - keep
            // draining so the server isn't cut off mid-response either way.
            da_http.result = -1;
            da_http.phase = DA_HTTP_DRAINING;
        }
    }
    return ERR_OK;
}

static void on_err(void *arg, err_t err){
    (void)arg; (void)err;
    // lwIP has already freed the pcb by the time tcp_err() runs - clear our
    // pointer first so device_auth_http_finish()'s cleanup doesn't touch it
    // (same rule socket_impl.c's own tcp_err handling follows).
    da_http.pcb = NULL;
    device_auth_http_finish(-1);
}

static err_t on_connected(void *arg, struct tcp_pcb *tpcb, err_t err){
    (void)arg;
    if (err != ERR_OK){
        device_auth_http_finish(-1);
        return ERR_OK;
    }

    err_t werr = tcp_write(tpcb, da_http.request, da_http.request_len, TCP_WRITE_FLAG_COPY);
    if (werr != ERR_OK){
        device_auth_http_finish(-1);
        return ERR_OK;
    }
    tcp_output(tpcb);
    da_http.phase = DA_HTTP_RECEIVING;
    return ERR_OK;
}

void net_device_auth_http_get_start(const unsigned char ip[4], uint16_t port, const char *request_line){
    device_auth_http_close_pcb();
    memset(&da_http, 0, sizeof(da_http));
    da_http.result = -1;

    int n = snprintf(da_http.request, sizeof(da_http.request),
        "%s\r\nHost: %u.%u.%u.%u\r\nConnection: close\r\n\r\n",
        request_line, ip[0], ip[1], ip[2], ip[3]);
    if (n <= 0 || (unsigned)n >= sizeof(da_http.request)){
        da_http.phase = DA_HTTP_FINISHED;
        return;
    }
    da_http.request_len = (unsigned)n;

    da_http.pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!da_http.pcb){
        da_http.phase = DA_HTTP_FINISHED;
        return;
    }
    tcp_arg(da_http.pcb, NULL);
    tcp_recv(da_http.pcb, on_recv);
    tcp_err(da_http.pcb, on_err);

    ip_addr_t dst;
    IP4_ADDR(ip_2_ip4(&dst), ip[0], ip[1], ip[2], ip[3]);

    da_http.phase = DA_HTTP_CONNECTING;
    da_http.started_at_us = time_us_64();

    cyw43_arch_lwip_begin();
    err_t err = tcp_connect(da_http.pcb, &dst, port, on_connected);
    cyw43_arch_lwip_end();
    if (err != ERR_OK){
        device_auth_http_finish(-1);
    }
}

bool net_device_auth_http_get_done(void){
    return da_http.phase == DA_HTTP_FINISHED;
}

int net_device_auth_http_get_result(void){
    return da_http.result;
}

// Called from picow_net.c's net_poll() on every main-loop iteration - only
// enforces the overall timeout; the connect/write/recv steps themselves are
// all driven by lwIP callbacks during cyw43_arch_poll(), which always runs
// first in net_poll() (see picow_net.c).
void picow_device_auth_http_poll(void){
    if (da_http.phase != DA_HTTP_CONNECTING && da_http.phase != DA_HTTP_RECEIVING &&
            da_http.phase != DA_HTTP_DRAINING) return;
    if (time_us_64() - da_http.started_at_us >= (uint64_t)DEVICE_AUTH_HTTP_TIMEOUT_MS * 1000){
        // If we were only draining (the status line was already parsed),
        // report that result instead of clobbering it with -1 - the actual
        // answer is still good even if the server took too long to close.
        device_auth_http_finish(da_http.phase == DA_HTTP_DRAINING ? da_http.result : -1);
    }
}
