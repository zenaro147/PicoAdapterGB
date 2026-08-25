#pragma once

#include <mobile.h>
#include <mobile_inet.h>

struct mobile_user;

// Per-connection socket handle contract. struct socket_impl is intentionally
// left incomplete here: only the selected implementation (see
// src/implementations/<impl>/net/) defines it and may dereference it. Core
// code (core/adapter_bridge.c, main.c, globals.h's struct mobile_user) only
// ever holds/passes struct socket_impl pointers.
//
// Porting to a different transport (e.g. a plain Pico + ESP32 over UART/SPI)
// only requires a new backend implementing this file's functions plus
// net_hal.h, selected at build time (see net/net_hal.h and
// src/implementations/picow/net/ for the current cyw43+lwIP backend).
struct socket_impl;

// Allocates/wires up mobile->socket[MOBILE_MAX_CONNECTIONS] with
// implementation-owned handles. Called once at boot, before libmobile ever
// requests a socket operation. Implementations are expected to use static
// storage (MOBILE_MAX_CONNECTIONS is a small libmobile-defined constant),
// not per-connection heap allocation.
void socket_hal_bind(struct mobile_user *mobile);

// Resets one connection's handle back to its idle/closed state. Used by
// main.c after a fresh Wi-Fi connection to clear out any state left over
// from a previous session, without main.c needing to know the handle's
// concrete layout.
void socket_hal_reset(struct socket_impl *state);

bool socket_impl_open(struct socket_impl *state, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport, void *user);
void socket_impl_close(struct socket_impl *state);
int socket_impl_connect(struct socket_impl *state, const struct mobile_addr *addr);
bool socket_impl_listen(struct socket_impl *state, void *user);
bool socket_impl_accept(struct socket_impl *state);
int socket_impl_send(struct socket_impl *state, const void *data, const unsigned size, const struct mobile_addr *addr);
int socket_impl_recv(struct socket_impl *state, void *data, unsigned size, struct mobile_addr *addr);

void socket_impl_close_commands(struct socket_impl *state);
