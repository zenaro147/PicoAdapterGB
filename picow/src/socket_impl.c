#include "socket_impl.h"
#include "picow_socket.h"

#include "pico/cyw43_arch.h"

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
            if(bindport != 0) state->tcp_pcb->local_port = bindport;
            break;
        case MOBILE_SOCKTYPE_UDP: 
            state->sock_type = SOCK_UDP;       
            state->udp_pcb = udp_new_ip_type(state->sock_addr);
            if(!state->udp_pcb) return false;            
            if(bindport != 0) state->udp_pcb->local_port = bindport;
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
    char srv_ip[46];
    memset(srv_ip,0x00,sizeof(srv_ip));

    //Check if is open
    if(state->sock_type == SOCK_TCP && state->tcp_pcb->state != CLOSED){
        switch (state->tcp_pcb->state){
            case ESTABLISHED:
                return 1;
                break;
            case SYN_SENT:
            case SYN_RCVD:
                return 0;
                break;
            default:
                return -1;
                break;
        }
    }

    if (addr->type == MOBILE_ADDRTYPE_IPV4) {
        const struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
        sprintf(srv_ip, "%u.%u.%u.%u", addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3]);
        if(state->sock_type == SOCK_TCP){
            err_t err = ERR_CLSD;
            ip4addr_aton(srv_ip,&state->tcp_pcb->remote_ip);
            state->tcp_pcb->remote_port = addr4->port;
            cyw43_arch_lwip_begin();
            err = tcp_connect(state->tcp_pcb, &state->tcp_pcb->remote_ip, state->tcp_pcb->remote_port, socket_connected_tcp);
            cyw43_arch_lwip_end();
            return 0;
        }else if (state->sock_type == SOCK_UDP){
            ip4addr_aton(srv_ip,&state->udp_pcb->remote_ip);
            state->udp_pcb->remote_port = addr4->port;
            return 1;
        }else{
            return -1;
        }
    } else if (addr->type == MOBILE_ADDRTYPE_IPV6) {
        const struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
        //Need to parse IPV6
        //sprintf(srv_ip, "%u:%u:%u:%u:%u:%u:%u:%u", addr6->host[0], addr6->host[1], addr6->host[2], addr6->host[3], addr6->host[4], addr6->host[5], addr6->host[6], addr6->host[7]);
        if(state->sock_type == SOCK_TCP){
            err_t err = ERR_CLSD;
            //ip6addr_aton(srv_ip,&state->tcp_pcb->remote_ip);
            state->tcp_pcb->remote_port = addr6->port;
            cyw43_arch_lwip_begin();
            err = tcp_connect(state->tcp_pcb, &state->tcp_pcb->remote_ip, state->tcp_pcb->remote_port, socket_connected_tcp);
            cyw43_arch_lwip_end();
            return 0;
            state->tcp_pcb->remote_port=addr6->port;
        }else if (state->sock_type == SOCK_UDP){
            //ip6addr_aton(srv_ip,&state->tcp_pcb->remote_ip);
            state->udp_pcb->remote_port=addr6->port;
        }else{
            return -1;
        }
    } else {
        return -1;
    }

}

bool socket_impl_listen(struct socket_impl *state){
}

bool socket_impl_accept(struct socket_impl *state){
}

int socket_impl_send(struct socket_impl *state, const void *data, const unsigned size, const struct mobile_addr *addr){
}

int socket_impl_recv(struct socket_impl *state, void *data, unsigned size, struct mobile_addr *addr){
}