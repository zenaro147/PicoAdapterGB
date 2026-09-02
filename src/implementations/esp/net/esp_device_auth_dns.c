// Standalone, non-blocking DNS type-A resolver implementing net_hal.h's
// net_device_auth_resolve_*() for this backend. Used only for the
// device-auth side channel's own server hostname (see core/adapter_bridge.c)
// - never seen by libmobile, and never sharing a link with mobile->socket[]
// (ESP_LINK_MOBILE_BASE..+1, see socket_impl.c): this claims its own link ID
// via ESP_LINK_OWNER_DEVICE_AUTH, the same arbitration esp_at.h already uses
// to keep the web module's own connections from colliding with libmobile's.
//
// Query construction/response parsing mirrors the approach already proven by
// the libmobile-bgb frontend's own equivalent resolver (a hand-built
// single-question type-A query, no compression-pointer following needed for
// this narrow case), adapted to this backend's bounded-blocking
// open/send + polled-non-blocking-recv shape (see esp_at.h) instead of
// blocking BSD sockets.
#include "net/net_hal.h"

#include <string.h>
#include <stdio.h>

#include "esp_at.h"
#include "globals.h"

#include <mobile.h>

#define DEVICE_AUTH_DNS_QUERY_MAX 300
#define DEVICE_AUTH_DNS_HEADER_SIZE 12
#define DEVICE_AUTH_DNS_TIMEOUT_MS 3000
#define DEVICE_AUTH_DNS_PORT 53

enum device_auth_dns_phase {
    DA_DNS_IDLE,
    DA_DNS_WAITING,
    DA_DNS_FINISHED,
};

struct device_auth_dns_state {
    enum device_auth_dns_phase phase;
    bool success;
    unsigned char result_ip[4];

    int link_id;

    struct mobile_addr4 servers[2];
    unsigned server_count;
    unsigned server_index;

    unsigned short query_id;
    unsigned char query[DEVICE_AUTH_DNS_QUERY_MAX];
    unsigned query_len;

    unsigned char resp_buf[512];
    unsigned resp_len; // bytes already collected via esp_at_recv() retries

    uint64_t sent_at_us;
};

static struct device_auth_dns_state da_dns;

static unsigned encode_name(unsigned char *out, const char *hostname){
    unsigned char *plen = out;
    unsigned char *pdat = plen + 1;
    unsigned count = 0;

    for (const char *c = hostname; *c; c++){
        if (*c == '.'){
            *plen = (unsigned char)count;
            count = 0;
            plen = pdat++;
        } else {
            *pdat++ = (unsigned char)*c;
            count++;
        }
    }
    *plen = (unsigned char)count;
    *pdat++ = 0;
    return (unsigned)(pdat - out);
}

static void build_query(const char *hostname){
    unsigned char *data = da_dns.query;

    data[0] = (da_dns.query_id >> 8) & 0xFF;
    data[1] = da_dns.query_id & 0xFF;
    static const unsigned char header[] = {
        0x01, 0x00, // flags: recursion desired
        0, 1,       // QDCOUNT = 1
        0, 0,       // ANCOUNT = 0
        0, 0,       // NSCOUNT = 0
        0, 0,       // ARCOUNT = 0
    };
    memcpy(data + 2, header, sizeof(header));

    unsigned offset = DEVICE_AUTH_DNS_HEADER_SIZE;
    offset += encode_name(data + offset, hostname);

    data[offset++] = 0; data[offset++] = 1; // QTYPE = A
    data[offset++] = 0; data[offset++] = 1; // QCLASS = IN

    da_dns.query_len = offset;
}

static void device_auth_dns_release_link(void){
    if (da_dns.link_id >= 0){
        esp_at_close(da_dns.link_id);
        da_dns.link_id = -1;
    }
}

static void device_auth_dns_finish(bool success){
    device_auth_dns_release_link();
    da_dns.phase = DA_DNS_FINISHED;
    da_dns.success = success;
}

static bool send_to_current_server(void){
    device_auth_dns_release_link();

    struct mobile_addr4 *addr4 = &da_dns.servers[da_dns.server_index];
    char host[16];
    snprintf(host, sizeof(host), "%u.%u.%u.%u",
        addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3]);

    da_dns.link_id = esp_at_link_alloc(ESP_LINK_OWNER_DEVICE_AUTH, -1);
    if (da_dns.link_id < 0) return false;

    if (!esp_at_udp_open(da_dns.link_id, host, DEVICE_AUTH_DNS_PORT, 0)){
        device_auth_dns_release_link();
        return false;
    }

    if (esp_at_send(da_dns.link_id, da_dns.query, da_dns.query_len) < 0){
        device_auth_dns_release_link();
        return false;
    }

    da_dns.resp_len = 0;
    da_dns.sent_at_us = time_us_64();
    return true;
}

static void try_next_server_or_fail(void){
    da_dns.server_index++;
    if (da_dns.server_index < da_dns.server_count && send_to_current_server()) return;
    device_auth_dns_finish(false);
}

static int name_field_len(const unsigned char *data, unsigned size, unsigned offset){
    if (offset + 1 > size) return -1;

    const unsigned char *p = data + offset;
    for (;;){
        if (!*p){
            p++;
            break;
        } else if ((*p & 0xC0) == 0xC0){
            unsigned pos = (unsigned)(p - data);
            if (pos + 2 > size) return -1;
            return (int)(pos + 2 - offset);
        } else if ((*p & 0xC0) == 0x00){
            unsigned len = *p++;
            if ((unsigned)(p - data) + len + 1 > size) return -1;
            p += len;
        } else {
            return -1;
        }
    }
    return (int)(p - (data + offset));
}

static void handle_response(void){
    unsigned char *buf = da_dns.resp_buf;
    unsigned size = da_dns.resp_len;

    if (size < DEVICE_AUTH_DNS_HEADER_SIZE) return; // ignore, keep waiting for a full response

    if ((unsigned)(buf[0] << 8 | buf[1]) != da_dns.query_id) return;

    unsigned flags = buf[2] << 8 | buf[3];
    if ((flags & 0xFB0F) != 0x8100){ // QR=1, no error, non-truncated
        device_auth_dns_finish(false);
        return;
    }

    unsigned qdcount = buf[4] << 8 | buf[5];
    unsigned ancount = buf[6] << 8 | buf[7];
    if (qdcount != 1 || ancount < 1){
        device_auth_dns_finish(false);
        return;
    }

    unsigned offset = DEVICE_AUTH_DNS_HEADER_SIZE + (da_dns.query_len - DEVICE_AUTH_DNS_HEADER_SIZE);
    if (offset > size){
        device_auth_dns_finish(false);
        return;
    }

    for (unsigned i = 0; i < ancount; i++){
        int name_len = name_field_len(buf, size, offset);
        if (name_len < 0) break;
        unsigned info = offset + (unsigned)name_len;
        if (info + 10 > size) break;

        unsigned type = buf[info] << 8 | buf[info + 1];
        unsigned class_ = buf[info + 2] << 8 | buf[info + 3];
        unsigned rdlength = buf[info + 8] << 8 | buf[info + 9];
        unsigned rdata = info + 10;
        if (rdata + rdlength > size) break;

        if (type == 1 && class_ == 1 && rdlength == 4){ // A / IN
            memcpy(da_dns.result_ip, buf + rdata, 4);
            device_auth_dns_finish(true);
            return;
        }

        offset = rdata + rdlength;
    }

    device_auth_dns_finish(false);
}

void net_device_auth_resolve_start(const char *hostname, const struct mobile_addr *dns1, const struct mobile_addr *dns2){
    device_auth_dns_release_link();
    memset(&da_dns, 0, sizeof(da_dns));
    da_dns.link_id = -1;

    if (dns1 && dns1->type == MOBILE_ADDRTYPE_IPV4) da_dns.servers[da_dns.server_count++] = *(const struct mobile_addr4 *)dns1;
    if (dns2 && dns2->type == MOBILE_ADDRTYPE_IPV4) da_dns.servers[da_dns.server_count++] = *(const struct mobile_addr4 *)dns2;
    if (da_dns.server_count == 0){
        da_dns.phase = DA_DNS_FINISHED;
        da_dns.success = false;
        return;
    }

    da_dns.query_id = (unsigned short)(time_us_64() & 0xFFFF);
    build_query(hostname);

    da_dns.server_index = 0;
    da_dns.phase = DA_DNS_WAITING;
    if (!send_to_current_server()){
        device_auth_dns_finish(false);
    }
}

bool net_device_auth_resolve_done(void){
    return da_dns.phase == DA_DNS_FINISHED;
}

bool net_device_auth_resolve_result(unsigned char ip[4]){
    if (da_dns.phase != DA_DNS_FINISHED || !da_dns.success) return false;
    memcpy(ip, da_dns.result_ip, 4);
    return true;
}

// Called from esp_net.c's net_poll() on every main-loop iteration. Drains
// the pending esp_at_recv() (non-blocking; 0 means AT+CIPRECVDATA is still
// in flight - see esp_at.h) and checks the current attempt's timeout.
void esp_device_auth_dns_poll(void){
    if (da_dns.phase != DA_DNS_WAITING) return;

    if (da_dns.resp_len < sizeof(da_dns.resp_buf)){
        int rc = esp_at_recv(da_dns.link_id, da_dns.resp_buf + da_dns.resp_len,
            sizeof(da_dns.resp_buf) - da_dns.resp_len);
        if (rc > 0){
            da_dns.resp_len += (unsigned)rc;
            handle_response();
            if (da_dns.phase != DA_DNS_WAITING) return;
        }
    }

    if (time_us_64() - da_dns.sent_at_us >= (uint64_t)DEVICE_AUTH_DNS_TIMEOUT_MS * 1000){
        try_next_server_or_fail();
    }
}
