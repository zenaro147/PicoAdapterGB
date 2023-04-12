#pragma once

#include <mobile.h>

struct socket_impl {
    uint8_t sock_addr;
    uint8_t sock_type;
    unsigned char buffer[MOBILE_MAX_TRANSFER_SIZE];
    int buffer_len;
	union{
		struct tcp_pcb *tcp_pcb;
        struct udp_pcb *udp_pcb;
	};
};

void socket_impl_init(struct socket_impl *state);
void socket_impl_stop(struct socket_impl *state);

bool socket_impl_open(struct socket_impl *state, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport);
void socket_impl_close(struct socket_impl *state);
int socket_impl_connect(struct socket_impl *state, const struct mobile_addr *addr);
bool socket_impl_listen(struct socket_impl *state);
bool socket_impl_accept(struct socket_impl *state);
int socket_impl_send(struct socket_impl *state, const void *data, const unsigned size, const struct mobile_addr *addr);
int socket_impl_recv(struct socket_impl *state, void *data, unsigned size, struct mobile_addr *addr);