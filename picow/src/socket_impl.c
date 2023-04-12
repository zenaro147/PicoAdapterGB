#include "socket_impl.h"
#include "picow_socket.h"

#include <string.h>

bool socket_impl_open(struct socket_impl *state, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport){
    switch (addrtype) {
        case MOBILE_ADDRTYPE_IPV4:
            state->sock_addr = IPADDR_TYPE_V4;
            break;
        case MOBILE_ADDRTYPE_IPV6:
            state->sock_addr = IPADDR_TYPE_V6;
            break;
        default: 
            return false;
    }
    
    switch (socktype) {
        case MOBILE_SOCKTYPE_TCP:
            state->sock_type = SOCK_TCP;
            state->tcp_pcb = tcp_new_ip_type(state->sock_addr);
            if(!state->tcp_pcb) return false;
            state->tcp_pcb->remote_port = bindport;
            break;
        case MOBILE_SOCKTYPE_UDP: 
            state->sock_type = SOCK_UDP;       
            state->udp_pcb = udp_new_ip_type(state->sock_addr);
            if(!state->udp_pcb) return false;
            state->udp_pcb->remote_port = bindport;
            break;
        default: 
            return false;
    }

    return true;
}

void socket_impl_close(struct socket_impl *state){
    bool sock_status = false;
    err_t err = ERR_ARG;
    switch (state->sock_type) {
        case SOCK_TCP:
            err = tcp_close(state->tcp_pcb);
            if (err != ERR_OK) {
                printf("close failed %d, calling abort\n", err);
                tcp_abort(state->tcp_pcb);
            }
            state->tcp_pcb = NULL;
            //tcp_arg(state->tcp_pcb, NULL);
            //tcp_poll(state->tcp_pcb, NULL, 0);
            tcp_accept(state->tcp_pcb, NULL);
            tcp_sent(state->tcp_pcb, NULL);
            tcp_recv(state->tcp_pcb, NULL);
            tcp_err(state->tcp_pcb, NULL);
            break;
        case SOCK_UDP:             
            udp_disconnect(state->udp_pcb);
            state->udp_pcb = NULL;
            udp_recv(state->udp_pcb, NULL, NULL);
            break;
        default: 
            break;
    }    
    state->sock_addr = -1;
    state->sock_type = -1;
    state->buffer_len = -1;
    memset(state->buffer,0x00,sizeof(state->buffer));
}

int socket_impl_connect(struct socket_impl *state, const struct mobile_addr *addr){
}

bool socket_impl_listen(struct socket_impl *state){
}

bool socket_impl_accept(struct socket_impl *state){
}

int socket_impl_send(struct socket_impl *state, const void *data, const unsigned size, const struct mobile_addr *addr){
}

int socket_impl_recv(struct socket_impl *state, void *data, unsigned size, struct mobile_addr *addr){
}