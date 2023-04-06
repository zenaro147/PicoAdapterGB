// SPDX-License-Identifier: GPL-3.0-or-later
#include "common.h"
#include "socket_impl.h"

#include <string.h>
#include <assert.h>
#include <stdio.h>

#include "socket.h"

union u_sockaddr {
    struct sockaddr addr;
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
};

void socket_impl_init(struct socket_impl *state)
{
    for (unsigned i = 0; i < MOBILE_MAX_CONNECTIONS; i++) {
        state->sockets[i] = -1;
    }
}

void socket_impl_stop(struct socket_impl *state)
{
    for (unsigned i = 0; i < MOBILE_MAX_CONNECTIONS; i++) {
        if (state->sockets[i] != -1) socket_close(state->sockets[i]);
    }
}

static struct sockaddr *convert_sockaddr(socklen_t *addrlen, union u_sockaddr *u_addr, const struct mobile_addr *addr)
{
    if (!addr) {
        *addrlen = 0;
        return NULL;
    } else if (addr->type == MOBILE_ADDRTYPE_IPV4) {
        const struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
        memset(&u_addr->addr4, 0, sizeof(u_addr->addr4));
        u_addr->addr4.sin_family = AF_INET;
        u_addr->addr4.sin_port = htons(addr4->port);
        if (sizeof(struct in_addr) != sizeof(addr4->host)) return NULL;
        memcpy(&u_addr->addr4.sin_addr.s_addr, addr4->host,
                sizeof(struct in_addr));
        *addrlen = sizeof(struct sockaddr_in);
        return &u_addr->addr;
    } else if (addr->type == MOBILE_ADDRTYPE_IPV6) {
        const struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
        memset(&u_addr->addr6, 0, sizeof(u_addr->addr6));
        u_addr->addr6.sin6_family = AF_INET6;
        u_addr->addr6.sin6_port = htons(addr6->port);
        if (sizeof(struct in6_addr) != sizeof(addr6->host)) return NULL;
        memcpy(&u_addr->addr6.sin6_addr.s6_addr, addr6->host,
                sizeof(struct in6_addr));
        *addrlen = sizeof(struct sockaddr_in6);
        return &u_addr->addr;
    } else {
        *addrlen = 0;
        return NULL;
    }
}

bool socket_impl_open(struct socket_impl *state, unsigned conn, enum mobile_socktype type, enum mobile_addrtype addrtype, unsigned bindport)
{
    assert(state->sockets[conn] == -1);

    int sock_type;
    switch (type) {
        case MOBILE_SOCKTYPE_TCP: sock_type = SOCK_STREAM; break;
        case MOBILE_SOCKTYPE_UDP: sock_type = SOCK_DGRAM; break;
        default: assert(false); return false;
    }

    int sock_addrtype;
    switch (addrtype) {
        case MOBILE_ADDRTYPE_IPV4: sock_addrtype = AF_INET; break;
        case MOBILE_ADDRTYPE_IPV6: sock_addrtype = AF_INET6; break;
        default: assert(false); return false;
    }

    int sock = socket(sock_addrtype, sock_type, 0);
    if (sock == -1) {
        socket_perror("socket");
        return false;
    }
    if (socket_setblocking(sock, 0) == -1) {
        socket_close(sock);
        return false;
    }

    // Set SO_REUSEADDR so that we can bind to the same port again after
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
            (char *)&(int){1}, sizeof(int)) == -1) {
        socket_perror("setsockopt");
        socket_close(sock);
        return false;
    }

    // Set TCP_NODELAY to aid sending packets inmediately, reducing latency
    if (type == MOBILE_SOCKTYPE_TCP &&
            setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                (char *)&(int){1}, sizeof(int)) == -1) {
        socket_perror("setsockopt");
        socket_close(sock);
        return false;
    }

    int rc;
    if (addrtype == MOBILE_ADDRTYPE_IPV4) {
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(bindport),
        };
        rc = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    } else {
        struct sockaddr_in6 addr = {
            .sin6_family = AF_INET6,
            .sin6_port = htons(bindport),
        };
        rc = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    }
    if (rc == -1) {
        socket_perror("bind");
        socket_close(sock);
        return false;
    }

    state->sockets[conn] = sock;
    return true;
}

void socket_impl_close(struct socket_impl *state, unsigned conn)
{
    assert(state->sockets[conn] != -1);
    socket_close(state->sockets[conn]);
    state->sockets[conn] = -1;
}

int socket_impl_connect(struct socket_impl *state, unsigned conn, const struct mobile_addr *addr)
{
    int sock = state->sockets[conn];
    assert(sock != -1);

    union u_sockaddr u_addr;
    socklen_t sock_addrlen;
    struct sockaddr *sock_addr = convert_sockaddr(&sock_addrlen, &u_addr, addr);

    // Try to connect/check if we're connected
    if (connect(sock, sock_addr, sock_addrlen) != -1) return 1;
    int err = socket_geterror();

    // If the connection is in progress, block at most 100ms to see if it's
    //   enough for it to connect.
    if (err == SOCKET_EWOULDBLOCK || err == SOCKET_EINPROGRESS
            || err == SOCKET_EALREADY) {
        int rc = socket_isconnected(sock, 100);
        if (rc > 0) return 1;
        if (rc == 0) return 0;
        err = socket_geterror();
    }

    char sock_host[INET6_ADDRSTRLEN] = {0};
    char sock_port[6] = {0};
    socket_straddr(sock_host, sizeof(sock_host), sock_port, sock_addr,
        sock_addrlen);
    socket_seterror(err);
    fprintf(stderr, "Could not connect (ip %s port %s): ",
        sock_host, sock_port);
    socket_perror(NULL);
    return -1;
}

bool socket_impl_listen(struct socket_impl *state, unsigned conn)
{
    int sock = state->sockets[conn];
    assert(sock != -1);

    if (listen(sock, 1) == -1) {
        socket_perror("listen");
        return false;
    }

    return true;
}

bool socket_impl_accept(struct socket_impl *state, unsigned conn){
    int sock = state->sockets[conn];
    assert(sock != -1);

    if (socket_hasdata(sock, 0) <= 0) return false;
    int newsock = accept(sock, NULL, NULL);
    if (newsock == -1) {
        socket_perror("accept");
        return false;
    }
    if (socket_setblocking(newsock, 0) == -1) return false;

    socket_close(sock);
    state->sockets[conn] = newsock;
    return true;
}

int socket_impl_send(struct socket_impl *state, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr)
{
    int sock = state->sockets[conn];
    assert(sock != -1);

    union u_sockaddr u_addr;
    socklen_t sock_addrlen;
    struct sockaddr *sock_addr = convert_sockaddr(&sock_addrlen, &u_addr, addr);

    ssize_t len = sendto(sock, data, size, 0, sock_addr, sock_addrlen);
    if (len == -1) {
        // If the socket is blocking, we just haven't sent anything
        int err = socket_geterror();
        if (err == SOCKET_EWOULDBLOCK) return 0;

        socket_perror("send");
        return -1;
    }
    return (int)len;
}

int socket_impl_recv(struct socket_impl *state, unsigned conn, void *data, unsigned size, struct mobile_addr *addr)
{
    int sock = state->sockets[conn];
    assert(sock != -1);

    // Make sure at least one byte is in the buffer
    if (socket_hasdata(sock, 0) <= 0) return 0;

    union u_sockaddr u_addr = {0};
    socklen_t sock_addrlen = sizeof(u_addr);
    struct sockaddr *sock_addr = (struct sockaddr *)&u_addr;

    ssize_t len;
    if (data) {
        // Retrieve at least 1 byte from the buffer
        len = recvfrom(sock, data, size, 0, sock_addr, &sock_addrlen);
    } else {
        // Check if at least 1 byte is available in buffer
        char c;
        len = recvfrom(sock, &c, 1, MSG_PEEK, sock_addr, &sock_addrlen);
    }
    if (len == -1) {
        // If the socket is blocking, we just haven't received anything
        // Though this shouldn't happen thanks to the socket_hasdata check.
        int err = socket_geterror();
        if (err == SOCKET_EWOULDBLOCK) return 0;

        socket_perror("recv");
        return -1;
    }

    // A length of 0 will be returned if the remote has disconnected.
    if (len == 0) {
        // Though it's only relevant to TCP sockets, as UDP sockets may receive
        // zero-length datagrams.
        int sock_type = 0;
        socklen_t sock_type_len = sizeof(sock_type);
        if (getsockopt(sock, SOL_SOCKET, SO_TYPE, (char *)&sock_type,
                &sock_type_len) == -1) {
            socket_perror("getsockopt");
            return -1;
        }
        if (sock_type == SOCK_STREAM) return -2;
    }

    if (!data) return 0;

    if (addr && sock_addrlen) {
        if (sock_addr->sa_family == AF_INET) {
            struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
            addr4->type = MOBILE_ADDRTYPE_IPV4;
            addr4->port = ntohs(u_addr.addr4.sin_port);
            memcpy(addr4->host, &u_addr.addr4.sin_addr.s_addr,
                sizeof(addr4->host));
        } else if (sock_addr->sa_family == AF_INET6) {
            struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
            addr6->type = MOBILE_ADDRTYPE_IPV6;
            addr6->port = ntohs(u_addr.addr6.sin6_port);
            memcpy(addr6->host, &u_addr.addr6.sin6_addr.s6_addr,
                sizeof(addr6->host));
        }
    }

    return (int)len;
}
