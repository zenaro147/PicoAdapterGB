#pragma once

// Concrete definition of the opaque struct socket_impl declared in
// net/socket_hal.h. Only this implementation (and its own picow_socket.c
// lwIP callbacks) may see these fields; core code only ever holds a pointer.
#include <mobile.h>
#include <mobile_inet.h>

#include "net/socket_hal.h"
#include "picow_socket.h"

// Opaque here on purpose (mirrors how core only ever sees struct socket_impl
// itself as opaque, via socket_hal.h) - only the pointer is needed below.
struct mobile_user;

struct socket_impl {
    // Set once by socket_hal_bind() and stable for the life of the program.
    // lwIP callbacks (picow_socket.c) use this to reach mobile_loop()'s
    // adapter - they must never resolve "which connection is this" through
    // any separately-tracked "current" index (see tcp_arg() below: each
    // pcb is bound directly to *this* socket_impl instead).
    struct mobile_user *mobile;
    uint8_t sock_addr;
    uint8_t sock_type;
    unsigned char udp_remote_ip[4]; // raw IPv4 host bytes, set directly from lwIP (no ASCII round-trip)
    unsigned udp_remote_port;
    bool client_status;
    bool inside_callback;
    bool pending_close;
    int socket_status;
    // 0 = no P2P connect retry in progress; otherwise the absolute deadline
    // (time_us_64()) for socket_impl_connect()'s silent connect-retry
    // window (see its comment) - a fresh peer refusal (RST) before this
    // opens a new pcb and retries instead of failing immediately.
    uint64_t connect_deadline_us;
    //uint8_t buffer_tx[MOBILE_MAX_TRANSFER_SIZE];
    uint8_t buffer_rx[BUFF_SIZE];
    int buffer_rx_len;
    int buffer_tx_len;
    uint16_t buffer_rx_read_pos; // per-socket read cursor into buffer_rx
	union{
		struct tcp_pcb *tcp_pcb;
        struct udp_pcb *udp_pcb;
	};
};
