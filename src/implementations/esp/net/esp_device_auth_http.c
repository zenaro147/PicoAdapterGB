// SPDX-License-Identifier: GPL-3.0-only
// Standalone, non-blocking one-shot HTTP GET implementing net_hal.h's
// net_device_auth_http_get_*() for this backend. Used only for the
// device-auth side channel (see core/adapter_bridge.c) - never seen by
// libmobile, and never sharing a link with mobile->socket[]
// (ESP_LINK_MOBILE_BASE..+1, see socket_impl.c): this claims its own link ID
// via ESP_LINK_OWNER_DEVICE_AUTH, the same owner esp_device_auth_dns.c
// already uses (they never run concurrently - see net_hal.h - so reusing the
// owner slot, not the link ID itself, is enough to keep both out of the
// mobile/web links' way).
#include "net/net_hal.h"

#include <string.h>
#include <stdio.h>

#include "esp_at.h"
#include "globals.h"

#define DEVICE_AUTH_HTTP_TIMEOUT_MS 5000
#define DEVICE_AUTH_HTTP_REQUEST_MAX 512
#define DEVICE_AUTH_HTTP_RESP_MAX 128
// "<server> <local> <sig>": up to 20 digits + space + 20 digits + space +
// 64 hex = 106; rounded up. ("blocked" in the first field is shorter than a
// counter, so the numeric form is the worst case.)
#define DEVICE_AUTH_HTTP_BODY_MAX 128

enum device_auth_http_phase {
    DA_HTTP_IDLE,
    DA_HTTP_CONNECTING,
    DA_HTTP_SENDING,
    DA_HTTP_RECEIVING, // no full status line parsed yet
    DA_HTTP_DRAINING,  // status line parsed; discarding the rest until the link closes
    DA_HTTP_FINISHED,
};

struct device_auth_http_state {
    enum device_auth_http_phase phase;
    int result; // -1 on failure, else the parsed HTTP status code

    int link_id;

    char request[DEVICE_AUTH_HTTP_REQUEST_MAX];
    unsigned request_len;

    char resp[DEVICE_AUTH_HTTP_RESP_MAX];
    unsigned resp_len;

    // Staging buffer every esp_at_recv() reads into, in both phases.
    // Deliberately a struct field and not a stack buffer: esp_at_recv()'s
    // contract (esp_at.h) requires the *same* buffer across repeated calls
    // while an AT+CIPRECVDATA may still be in flight.
    char rx[DEVICE_AUTH_HTTP_RESP_MAX];

    // Response body, kept apart from the headers - see the picow backend for
    // the full reasoning. "<counter> <sig>" is at most 85 bytes, so sizing
    // for that beats buffering headers whose length the server controls.
    char body[DEVICE_AUTH_HTTP_BODY_MAX];
    unsigned body_len;
    unsigned hdr_match;
    bool body_started;

    uint64_t started_at_us;
};

static struct device_auth_http_state da_http;

static void device_auth_http_release_link(void){
    if (da_http.link_id >= 0){
        esp_at_close(da_http.link_id);
        da_http.link_id = -1;
    }
}

static void device_auth_http_finish(int result){
    device_auth_http_release_link();
    da_http.phase = DA_HTTP_FINISHED;
    da_http.result = result;
}

// Parses "HTTP/1.x SSS <reason>" - scans for the first space rather than
// assuming a fixed offset, so it doesn't care whether the server said
// HTTP/1.0 or HTTP/1.1.
// Splits the response stream into headers and body as it arrives; identical
// in behaviour to the picow backend's own copy. Byte-at-a-time because the
// "\r\n\r\n" separator can straddle two reads.
static void device_auth_http_consume(const char *data, unsigned len){
    for (unsigned i = 0; i < len; i++){
        char c = data[i];

        if (da_http.body_started){
            if (da_http.body_len < sizeof(da_http.body)){
                da_http.body[da_http.body_len++] = c;
            }
            continue;
        }

        if (da_http.resp_len < sizeof(da_http.resp)){
            da_http.resp[da_http.resp_len++] = c;
        }

        static const char sep[4] = {'\r', '\n', '\r', '\n'};
        if (c == sep[da_http.hdr_match]){
            da_http.hdr_match++;
            if (da_http.hdr_match == 4) da_http.body_started = true;
        } else {
            da_http.hdr_match = (c == '\r') ? 1 : 0;
        }
    }
}

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

// Returns true once the status line has been found (or given up on) and
// da_http.result/phase have been updated accordingly. Deliberately does NOT
// call device_auth_http_finish() itself (which would close the link) - see
// the DA_HTTP_DRAINING case in esp_device_auth_http_poll() for why: closing
// as soon as the status line is spotted, while the server may still be
// sending the rest of the response, is a real bug found on this exact
// endpoint by a sibling frontend (logged there as spurious 499/RST
// server-side). The link is only released once the peer closes it.
static bool try_parse_buffered_response(void){
    for (unsigned i = 0; i + 1 < da_http.resp_len; i++){
        if (da_http.resp[i] == '\r' && da_http.resp[i + 1] == '\n'){
            da_http.result = parse_status_line(da_http.resp, i);
            da_http.phase = DA_HTTP_DRAINING;
            return true;
        }
    }
    if (da_http.resp_len >= sizeof(da_http.resp)){
        // Status line never terminated within our small buffer. We don't
        // know the code, but still don't close early - keep draining so the
        // server isn't cut off mid-response either way.
        da_http.result = -1;
        da_http.phase = DA_HTTP_DRAINING;
        return true;
    }
    return false;
}

void net_device_auth_http_get_start(const unsigned char ip[4], uint16_t port, const char *request_line){
    device_auth_http_release_link();
    memset(&da_http, 0, sizeof(da_http));
    da_http.link_id = -1;
    da_http.result = -1;

    int n = snprintf(da_http.request, sizeof(da_http.request),
        "%s\r\nHost: %u.%u.%u.%u\r\nConnection: close\r\n\r\n",
        request_line, ip[0], ip[1], ip[2], ip[3]);
    if (n <= 0 || (unsigned)n >= sizeof(da_http.request)){
        da_http.phase = DA_HTTP_FINISHED;
        return;
    }
    da_http.request_len = (unsigned)n;

    da_http.link_id = esp_at_link_alloc(ESP_LINK_OWNER_DEVICE_AUTH, -1);
    if (da_http.link_id < 0){
        da_http.phase = DA_HTTP_FINISHED;
        return;
    }

    char host[16];
    snprintf(host, sizeof(host), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

    int rc = esp_at_tcp_connect(da_http.link_id, host, port);
    da_http.phase = DA_HTTP_CONNECTING;
    da_http.started_at_us = time_us_64();
    if (rc < 0){
        device_auth_http_finish(-1);
    }
    // rc == 0 (still connecting) or rc == 1 (connected already): both are
    // handled uniformly by esp_device_auth_http_poll() below, so there's
    // nothing else to do here.
}

bool net_device_auth_http_get_done(void){
    return da_http.phase == DA_HTTP_FINISHED;
}

int net_device_auth_http_get_result(void){
    return da_http.result;
}

const char *net_device_auth_http_get_body(unsigned *len){
    *len = da_http.body_len;
    return da_http.body;
}

// Called from esp_net.c's net_poll() on every main-loop iteration. Drives
// the connect -> send -> recv sequence and enforces the overall timeout.
void esp_device_auth_http_poll(void){
    switch (da_http.phase){
        case DA_HTTP_CONNECTING: {
            int rc = esp_at_tcp_connect(da_http.link_id, NULL, 0);
            if (rc < 0){
                device_auth_http_finish(-1);
            } else if (rc == 1){
                da_http.phase = DA_HTTP_SENDING;
            }
            break;
        }
        case DA_HTTP_SENDING: {
            int sent = esp_at_send(da_http.link_id, da_http.request, da_http.request_len);
            if (sent < 0){
                device_auth_http_finish(-1);
            } else if (sent > 0){
                da_http.phase = DA_HTTP_RECEIVING;
            }
            break;
        }
        case DA_HTTP_RECEIVING: {
            int rc = esp_at_recv(da_http.link_id, da_http.rx, sizeof(da_http.rx));
            if (rc > 0){
                device_auth_http_consume(da_http.rx, (unsigned)rc);
                try_parse_buffered_response(); // may switch phase to DA_HTTP_DRAINING
            } else if (rc < 0){
                // Link closed by the remote before a full status line ever
                // arrived - parse whatever partial bytes we have, if any
                // (matches DA_HTTP_DRAINING's own finish path).
                device_auth_http_finish(da_http.resp_len > 0 ?
                    parse_status_line(da_http.resp, da_http.resp_len) : -1);
                return;
            }
            break;
        }
        case DA_HTTP_DRAINING: {
            // No longer discards: the body arrives during this phase and the
            // device-auth counter query needs it, so keep feeding the same
            // splitter. Reads land in da_http.rx because esp_at_recv()'s
            // contract requires the *same* buffer across repeated calls while
            // an AT+CIPRECVDATA may still be in flight (see esp_at.h), which
            // rules out a fresh stack buffer each poll.
            int rc = esp_at_recv(da_http.link_id, da_http.rx, sizeof(da_http.rx));
            if (rc > 0){
                device_auth_http_consume(da_http.rx, (unsigned)rc);
            } else if (rc < 0){
                // Remote closed - the normal end of a Connection: close
                // response. Report the result already parsed above.
                device_auth_http_finish(da_http.result);
                return;
            }
            break;
        }
        default:
            return;
    }

    if (da_http.phase != DA_HTTP_FINISHED &&
            time_us_64() - da_http.started_at_us >= (uint64_t)DEVICE_AUTH_HTTP_TIMEOUT_MS * 1000){
        // If we were only draining (the status line was already parsed),
        // report that result instead of clobbering it with -1.
        device_auth_http_finish(da_http.phase == DA_HTTP_DRAINING ? da_http.result : -1);
    }
}
