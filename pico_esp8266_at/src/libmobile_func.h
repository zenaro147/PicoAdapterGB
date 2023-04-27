#pragma once

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"

#include <mobile.h>

#define TYPE_NONE 0
#define TYPE_TCP 1
#define TYPE_UDP 2

#define ADDR_NONE 0
#define ADDR_IPV4 1
#define ADDR_IPV6 2

extern int gval;

struct esp_sock_config {
    int host_id;
    uint8_t host_type; //0=NONE, 1=TCP or 2=UDP
    uint8_t host_iptype; //IPV4, IPV6 or NONE
    char host_addr[60];
    int local_port;
    int remote_port;
    bool sock_status;
    bool isServerOpened;
    int ipdVal;
};

void socket_impl_init(struct esp_sock_config *state);
void socket_impl_stop(struct esp_sock_config *state);

bool socket_impl_open(struct esp_sock_config *state, unsigned conn, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport);
void socket_impl_close(struct esp_sock_config *state);
int socket_impl_connect(struct esp_sock_config *state, const struct mobile_addr *addr);
bool socket_impl_listen(struct esp_sock_config *state);
bool socket_impl_accept(struct esp_sock_config *state);
int socket_impl_send(struct esp_sock_config *state, const void *data, const unsigned size, const struct mobile_addr *addr);
int socket_impl_recv(struct esp_sock_config *state, void *data, unsigned size, struct mobile_addr *addr);