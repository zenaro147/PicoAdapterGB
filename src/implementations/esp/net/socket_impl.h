#pragma once

// Concrete definition of the opaque struct socket_impl declared in
// net/socket_hal.h. Only this file's own socket_impl.c may see these fields;
// core code only ever holds a pointer (see net/socket_hal.h).
#include <mobile.h>
#include <mobile_inet.h>

#include "net/socket_hal.h"

#define SOCK_NONE -1
#define SOCK_TCP 1
#define SOCK_UDP 2

struct socket_impl {
    int link_id;         // ESP-AT link ID (0..ESP_LINK_COUNT-1), fixed per mobile connection
    int sock_type;        // SOCK_NONE / SOCK_TCP / SOCK_UDP
    int sock_addr;         // MOBILE_ADDRTYPE_IPV4 once opened (IPv6 is not supported - see README.md)
    unsigned bindport;
    bool listening;        // socket_impl_listen() was called, waiting on socket_impl_accept()
    bool connect_in_progress;
    char remote_host[16];  // cached dotted-decimal peer, for UDP retargeting on send()
    unsigned remote_port;
    // 0 = no P2P connect retry in progress; otherwise the absolute deadline
    // (time_us_64()) for socket_impl_connect()'s silent connect-retry
    // window (see its comment) - a fresh peer refusal before this opens a
    // new link and retries instead of failing immediately.
    uint64_t connect_deadline_us;
};
