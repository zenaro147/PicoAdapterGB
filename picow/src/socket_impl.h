// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "common.h"

#include <mobile.h>

struct socket_impl {
    int sockets[MOBILE_MAX_CONNECTIONS];
};

void socket_impl_init(struct socket_impl *state);
void socket_impl_stop(struct socket_impl *state);

bool socket_impl_open(struct socket_impl *state, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport);
void socket_impl_close(struct socket_impl *state, unsigned conn);
int socket_impl_connect(struct socket_impl *state, unsigned conn, const struct mobile_addr *addr);
bool socket_impl_listen(struct socket_impl *state, unsigned conn);
bool socket_impl_accept(struct socket_impl *state, unsigned conn);
int socket_impl_send(struct socket_impl *state, unsigned conn, const void *data, const unsigned size, const struct mobile_addr *addr);
int socket_impl_recv(struct socket_impl *state, unsigned conn, void *data, unsigned size, struct mobile_addr *addr);
