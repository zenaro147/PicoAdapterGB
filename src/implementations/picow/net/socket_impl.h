#pragma once

// Concrete definition of the opaque struct socket_impl declared in
// net/socket_hal.h. Only this implementation (and its own picow_socket.c
// lwIP callbacks) may see these fields; core code only ever holds a pointer.
#include <mobile.h>
#include <mobile_inet.h>

#include "net/socket_hal.h"
#include "picow_socket.h"

struct socket_impl {
    uint8_t sock_addr;
    uint8_t sock_type;
    unsigned char udp_remote_ip[4]; // raw IPv4 host bytes, set directly from lwIP (no ASCII round-trip)
    unsigned udp_remote_port;
    bool client_status;
    bool inside_callback;
    bool pending_close;
    int socket_status;
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
