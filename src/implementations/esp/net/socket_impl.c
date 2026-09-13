// SPDX-License-Identifier: GPL-3.0-only
// net/socket_hal.h implementation for the esp backend: translates libmobile's
// socket contract into ESP-AT commands via esp_at.h. See esp_at.h for which
// calls are async (non-blocking, called repeatedly - connect/send/recv) vs.
// bounded-blocking (setup/lifecycle calls: Wi-Fi join, server start/stop,
// socket close), and README.md for the ESP8266 ESP-AT limitations this has
// to work around (IPv4 only, one shared CIPSERVER slot).
#include "socket_impl.h"
#include "esp_at.h"
#include "esp_config.h"
#include "globals.h"

#include <stdio.h>
#include <string.h>

// Static storage for every connection's handle: MOBILE_MAX_CONNECTIONS is a
// small libmobile-defined constant (currently 2), so this avoids heap
// allocation/fragmentation for state that lives for the whole program.
static struct socket_impl socket_storage[MOBILE_MAX_CONNECTIONS];

void socket_hal_bind(struct mobile_user *mobile){
    for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
        socket_storage[i].link_id = ESP_LINK_MOBILE_BASE + i;
        mobile->socket[i] = &socket_storage[i];
    }
}

void socket_hal_reset(struct socket_impl *state){
    esp_at_link_release(state->link_id);
    state->sock_type = SOCK_NONE;
    state->sock_addr = -1;
    state->bindport = 0;
    state->listening = false;
    state->connect_in_progress = false;
    state->remote_host[0] = '\0';
    state->remote_port = 0;
}

static void format_ipv4(const struct mobile_addr4 *addr4, char *dest, size_t destsize){
    snprintf(dest, destsize, "%u.%u.%u.%u",
        addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3]);
}

bool socket_impl_open(struct socket_impl *state, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport, void *user){
    (void)user;
    if (state->sock_type != SOCK_NONE) return false;

    if (addrtype != MOBILE_ADDRTYPE_IPV4) {
        // IPv6 is not implemented for this backend (ESP8266 ESP-AT 2.3.0.0's
        // IPv6 support is partial/inconsistent and unused by this project -
        // see README.md). libmobile's own contract allows failing IPv6.
        return false;
    }

    if (esp_at_link_alloc(ESP_LINK_OWNER_MOBILE, state->link_id) != state->link_id) {
        // The reserved link ID was unexpectedly taken (see README.md's note
        // on the web server's auto-assigned IDs potentially colliding).
        return false;
    }

    state->sock_addr = MOBILE_ADDRTYPE_IPV4;
    state->sock_type = (socktype == MOBILE_SOCKTYPE_TCP) ? SOCK_TCP : SOCK_UDP;
    state->bindport = bindport;
    state->listening = false;
    state->connect_in_progress = false;
    state->remote_host[0] = '\0';
    state->remote_port = 0;
    state->connect_deadline_us = 0;
    return true;
}

void socket_impl_close(struct socket_impl *state){
    if (state->sock_type == SOCK_NONE) return;
    esp_at_close(state->link_id);
    state->sock_type = SOCK_NONE;
    state->sock_addr = -1;
    state->listening = false;
    state->connect_in_progress = false;
    state->remote_host[0] = '\0';
    state->remote_port = 0;
}

int socket_impl_connect(struct socket_impl *state, const struct mobile_addr *addr){
    if (state->sock_type == SOCK_NONE) return -1;
    if (addr->type != MOBILE_ADDRTYPE_IPV4) return -1;

    const struct mobile_addr4 *addr4 = (const struct mobile_addr4 *)addr;
    char host[16];
    format_ipv4(addr4, host, sizeof(host));

    if (state->sock_type == SOCK_UDP) {
        // UDP "connect" is just setting the default peer (see mobile.h);
        // ESP-AT's UDP CIPSTART itself returns immediately (no handshake).
        if (!esp_at_udp_set_remote(state->link_id, host, addr4->port)) return -1;
        strncpy(state->remote_host, host, sizeof(state->remote_host) - 1);
        state->remote_host[sizeof(state->remote_host) - 1] = '\0';
        state->remote_port = addr4->port;
        return 1;
    }

    if (!state->connect_in_progress) {
        if (esp_at_link_is_connected(state->link_id)) return 1; // already connected
        strncpy(state->remote_host, host, sizeof(state->remote_host) - 1);
        state->remote_host[sizeof(state->remote_host) - 1] = '\0';
        state->remote_port = addr4->port;
        state->connect_in_progress = true;
        state->connect_deadline_us = 0; // fresh dial sequence - see below
    }

    int rc = esp_at_tcp_connect(state->link_id, state->remote_host, (uint16_t)state->remote_port);
    if (rc < 0) {
        // Mirror picow's P2P connect-retry behavior (net/socket_impl.c
        // there, TCP_CONNECT_RETRY_WINDOW_MS) instead of failing on the
        // first refusal: a real Mobile Adapter P2P peer may simply not be
        // listening *yet*. See esp_config.h's ESP_TCP_CONNECT_RETRY_WINDOW_MS
        // for why this stays comfortably inside dependences/libmobile's own
        // 60s command_tel_ip() ceiling.
        if (state->connect_deadline_us == 0) {
            state->connect_deadline_us = time_us_64() + MS(ESP_TCP_CONNECT_RETRY_WINDOW_MS);
        }
        if (time_us_64() >= state->connect_deadline_us) {
            state->connect_deadline_us = 0;
            state->connect_in_progress = false;
            return -1;
        }
        // A failed AT+CIPSTART leaves this link "dirty" on the module until
        // explicitly closed (see esp_at_close()'s .in_use gate and its own
        // comment) - retrying AT+CIPSTART on the same link_id without this
        // would fail immediately every time, defeating the retry entirely.
        esp_at_close(state->link_id);
        return 0; // keep libmobile polling; retried on the next call
    }
    if (rc > 0) {
        state->connect_deadline_us = 0;
        state->connect_in_progress = false;
    }
    return rc;
}

int socket_impl_send(struct socket_impl *state, const void *data, const unsigned size, const struct mobile_addr *addr){
    if (state->sock_type == SOCK_NONE) return -1;

    if (state->sock_type == SOCK_UDP && addr) {
        if (addr->type != MOBILE_ADDRTYPE_IPV4) return -1;
        const struct mobile_addr4 *addr4 = (const struct mobile_addr4 *)addr;
        char host[16];
        format_ipv4(addr4, host, sizeof(host));
        if (strcmp(host, state->remote_host) != 0 || addr4->port != state->remote_port) {
            if (!esp_at_udp_set_remote(state->link_id, host, addr4->port)) return -1;
            strncpy(state->remote_host, host, sizeof(state->remote_host) - 1);
            state->remote_host[sizeof(state->remote_host) - 1] = '\0';
            state->remote_port = addr4->port;
        }
    }

    return esp_at_send(state->link_id, data, size);
}

int socket_impl_recv(struct socket_impl *state, void *data, unsigned size, struct mobile_addr *addr){
    if (state->sock_type == SOCK_NONE) return -1;

    if (!data) {
        // "Is the connection still alive?" check (see mobile.h's sock_recv doc).
        if (state->sock_type != SOCK_TCP) return 0;
        if (esp_at_link_consume_closed_event(state->link_id)) return -2;
        return esp_at_link_is_connected(state->link_id) ? 0 : -2;
    }

    if (esp_at_link_consume_closed_event(state->link_id) && esp_at_link_rx_pending(state->link_id) == 0) {
        return -2;
    }

    if (addr && state->sock_type == SOCK_UDP) {
        struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
        addr4->type = MOBILE_ADDRTYPE_IPV4;
        addr4->port = (unsigned)state->remote_port;
        // The peer that sent the buffered datagram isn't tracked separately
        // from the socket's own default remote in this backend (ESP-AT's
        // passive receive model reports it per-link, not per-datagram) -
        // this only differs from the connect()ed peer if the remote address
        // changes between UDP datagrams on the same link, which this project
        // does not rely on for Mobile Adapter GB traffic.
        unsigned a, b, c, d;
        if (sscanf(state->remote_host, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
            addr4->host[0] = (unsigned char)a;
            addr4->host[1] = (unsigned char)b;
            addr4->host[2] = (unsigned char)c;
            addr4->host[3] = (unsigned char)d;
        }
    }

    int rc = esp_at_recv(state->link_id, data, size);
    if (rc == 0 && state->sock_type == SOCK_TCP && !esp_at_link_is_connected(state->link_id)
            && esp_at_link_rx_pending(state->link_id) == 0) {
        return -2;
    }
    if (rc < 0 && state->sock_type == SOCK_TCP && !esp_at_link_is_connected(state->link_id)) {
        // AT+CIPRECVDATA can fail with a plain ERROR (not "0 bytes, OK")
        // when the remote closes the connection right around the time we
        // ask for more data - confirmed on hardware against a real
        // non-keep-alive HTTP server closing right after its last bytes.
        // That's an expected end of stream, not a real transport error:
        // reinterpret it as the standard "remote closed" signal (like the
        // rc==0 case above) rather than surfacing esp_at_recv()'s -1 and
        // making libmobile report a hard failure to the Game Boy for what
        // was actually a normal, complete transfer.
        esp_at_link_consume_closed_event(state->link_id);
        return -2;
    }
    return rc;
}

bool socket_impl_listen(struct socket_impl *state, void *user){
    (void)user;
    if (state->sock_type != SOCK_TCP) return false;
    if (!esp_at_server_start((uint16_t)state->bindport, ESP_LINK_OWNER_MOBILE)) return false;
    state->listening = true;
    return true;
}

bool socket_impl_accept(struct socket_impl *state){
    if (!state->listening) return false;
    int accepted_link = esp_at_server_accept(ESP_LINK_OWNER_MOBILE);
    if (accepted_link < 0) return false;

    // Discard the original listening link's reservation and adopt the
    // accepted connection's link ID instead (see mobile.h's sock_accept doc:
    // "This automatically discards the listening socket upon success").
    esp_at_link_release(state->link_id);
    state->link_id = accepted_link;
    state->listening = false;
    esp_at_server_stop(ESP_LINK_OWNER_MOBILE); // single shared slot - see README.md
    return true;
}

void socket_impl_close_commands(struct socket_impl *state){
    socket_impl_close(state);
}
