#include "socket_impl.h"
#include "picow_socket.h"

#include "pico/cyw43_arch.h"

#include <string.h>

uint16_t buffrx_lastpos = 0;

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
            
            tcp_arg(state->tcp_pcb, state);
            //tcp_poll(state->tcp_pcb, NULL, 0);
            tcp_accept(state->tcp_pcb, socket_accept_tcp);
            tcp_sent(state->tcp_pcb, socket_sent_tcp);
            tcp_recv(state->tcp_pcb, socket_recv_tcp);
            tcp_err(state->tcp_pcb, socket_err_tcp);

            break;
        case MOBILE_SOCKTYPE_UDP: 
            state->sock_type = SOCK_UDP;       
            state->udp_pcb = udp_new_ip_type(state->sock_addr);
            if(!state->udp_pcb) return false;            
            if(bindport != 0) state->udp_pcb->local_port = bindport;

            udp_recv(state->udp_pcb, socket_recv_udp, state);

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
            tcp_arg(state->tcp_pcb, NULL);
            //tcp_poll(state->tcp_pcb, NULL, 0);
            tcp_accept(state->tcp_pcb, NULL);
            tcp_sent(state->tcp_pcb, NULL);
            tcp_recv(state->tcp_pcb, NULL);
            tcp_err(state->tcp_pcb, NULL);
            state->tcp_pcb = NULL;
            break;
        case SOCK_UDP:             
            udp_disconnect(state->udp_pcb);
            state->udp_pcb = NULL;
            udp_recv(state->udp_pcb, NULL, NULL);
            break;
        default: 
            break;
    }        
    state->tcp_pcb = NULL;
    state->udp_pcb = NULL;
    state->sock_addr = -1;
    state->sock_type = -1;
    memset(state->udp_remote_srv,0x00,sizeof(state->udp_remote_srv));
    state->udp_remote_port = 0;
    state->client_status = false;
    memset(state->buffer_rx,0x00,sizeof(state->buffer_rx));
    memset(state->buffer_tx,0x00,sizeof(state->buffer_tx));
    state->buffer_rx_len = 0;
    state->buffer_tx_len = 0;
    printf("Socket Closed.\n");
}

int socket_impl_connect(struct socket_impl *state, const struct mobile_addr *addr){
    //Check if everything is OK to connect
    if( 
        (addr->type == MOBILE_ADDRTYPE_IPV4 && state->sock_addr == IPADDR_TYPE_V6) || 
        (addr->type == MOBILE_ADDRTYPE_IPV6 && state->sock_addr == IPADDR_TYPE_V4)){
        return -1;
    }
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
            if(err != ERR_OK){
                printf("Connect TCP failed %d\n", err);
                return -1;
            } 
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
            if(err != ERR_OK){
                printf("Connect TCP failed %d\n", err);
                return -1;
            } 
        }else if (state->sock_type == SOCK_UDP){
            //ip6addr_aton(srv_ip,&state->tcp_pcb->remote_ip);
            state->udp_pcb->remote_port=addr6->port;
        }else{
            return -1;
        }
    } else {
        return -1;
    }
    return 0;
}

int socket_impl_send(struct socket_impl *state, const void *data, const unsigned size, const struct mobile_addr *addr){
    //Check if everything is OK to send
    if( 
        (state->sock_type == SOCK_TCP && (state->tcp_pcb->state == CLOSED || !state->tcp_pcb)) || 
        (state->sock_type == SOCK_UDP && (!state->udp_pcb && !addr) ||
        (addr && (addr->type == MOBILE_ADDRTYPE_IPV4 && state->sock_addr == IPADDR_TYPE_V6) || 
        (addr->type == MOBILE_ADDRTYPE_IPV6 && state->sock_addr == IPADDR_TYPE_V4)))){
        // printf("teste1\n");
        return -1;
    }

    // printf("teste2\n");
    err_t err = ERR_ARG;
    if(state->sock_type == SOCK_TCP){
        // printf("teste3\n");
        cyw43_arch_lwip_begin();
        err = tcp_write(state->tcp_pcb,data,size,TCP_WRITE_FLAG_COPY);
        cyw43_arch_lwip_end();
    }else if(state->sock_type == SOCK_UDP) {
        // printf("teste4\n");
        //Set a new IP/Port if receive an addr parameter
        if(addr){
            // printf("teste5\n");
            char srv_ip[46];
            memset(srv_ip,0x00,sizeof(srv_ip));
            if (addr->type == MOBILE_ADDRTYPE_IPV4) {
                const struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
                sprintf(srv_ip, "%u.%u.%u.%u", addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3]);
                ip4addr_aton(srv_ip, &state->tcp_pcb->remote_ip);
                state->udp_pcb->remote_port=addr4->port;
            } else if (addr->type == MOBILE_ADDRTYPE_IPV6) {
                const struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
                //sprintf(srv_ip, "%u:%u:%u:%u:%u:%u:%u:%u", addr6->host[0], addr6->host[1], addr6->host[2], addr6->host[3], addr6->host[4], addr6->host[5], addr6->host[6], addr6->host[7]);
                //ip6addr_aton(srv_ip,&state->tcp_pcb->remote_ip);
                state->udp_pcb->remote_port=addr6->port;
            }else{
                return -1; 
            }
        }
        printf("Sending %d\n",size);
        // struct pbuf * p = pbuf_alloc(PBUF_TRANSPORT,size+1,PBUF_RAM);
        // uint8_t *pt = (uint8_t *) p->payload;
        // memcpy(pt,data,size);
        // pt[size]='\0';

        struct pbuf * p = pbuf_alloc(PBUF_TRANSPORT,size,PBUF_RAM);
        uint8_t *pt = (uint8_t *) p->payload;
        memcpy(pt,data,size);
        
        cyw43_arch_lwip_begin();
        err = udp_send(state->udp_pcb,p);
        cyw43_arch_lwip_end();
        pbuf_free(p);
    }else{
        // printf("teste6\n");
        return -1;
    }
    if(err != ERR_OK){
        printf("Send failed %d\n", err);
        return -1;
    } 
    state->buffer_rx_len = 0;
    printf("reset buffRX size\n");
    state->buffer_tx_len = state->buffer_tx_len + size;
    // printf("teste - %d\n",size);
    return size;
}

int socket_impl_recv(struct socket_impl *state, void *data, unsigned size, struct mobile_addr *addr){
    //If the socket is a TCP and don't have any buff, check if it's disconnected to return an error
    if(state->sock_type == SOCK_TCP && state->buffer_rx_len <= 0){
        // printf("teste1\n");
        if(!data){
            // printf("teste2\n");
            // CLOSED      = 0,
            // LISTEN      = 1,
            // SYN_SENT    = 2,
            // SYN_RCVD    = 3,
            // ESTABLISHED = 4,
            // FIN_WAIT_1  = 5,
            // FIN_WAIT_2  = 6,
            // CLOSE_WAIT  = 7,
            // CLOSING     = 8,
            // LAST_ACK    = 9,
            // TIME_WAIT   = 10
            switch (state->tcp_pcb->state){
                case ESTABLISHED:
                case LISTEN:
                case SYN_SENT:
                case SYN_RCVD:
                    return 0;
                    break;
                case CLOSED:
                case CLOSING:
                case CLOSE_WAIT:
                    return -2;
                    break;
                default:
                    return -1;
                    break;
            }
        }
        if((state->tcp_pcb->state == CLOSED || !state->tcp_pcb)){
            // printf("teste3\n");
            return -2;
        }     
    }

    // printf("teste4\n");
    int recvd_buff = 0;
    if(state->buffer_rx_len > 0){
        // printf("teste5\n");
        if (addr && state->sock_type == SOCK_UDP){
            // printf("teste6\n");
            struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
            struct mobile_addr6 *addr6 = (struct mobile_addr6 *)addr;
            unsigned char ip[MOBILE_INET_PTON_MAXLEN];
            int rc = mobile_inet_pton(MOBILE_INET_PTON_ANY, state->udp_remote_srv, ip);

            switch (rc) {
                case MOBILE_INET_PTON_IPV4:
                    addr4->type = MOBILE_ADDRTYPE_IPV4;
                    addr4->port = state->udp_remote_port;
                    memcpy(addr4->host, ip, sizeof(addr4->host));
                    break;
                case MOBILE_INET_PTON_IPV6:
                    addr6->type = MOBILE_ADDRTYPE_IPV6;
                    addr6->port = state->udp_remote_port;
                    memcpy(addr6->host, ip, sizeof(addr6->host));
                    break;
                default:
                    break;
            }
        }
        
        uint16_t tmpsize = state->buffer_rx_len - buffrx_lastpos;
        if(tmpsize > MOBILE_MAX_TRANSFER_SIZE){
            // printf("teste7\n");
            recvd_buff = MOBILE_MAX_TRANSFER_SIZE;
        }else{
            // printf("teste8\n");
            recvd_buff = tmpsize;
        }        
        if (recvd_buff > size) recvd_buff = size;

        printf("copied %d bytes\n",recvd_buff);
        memcpy(data,state->buffer_rx + buffrx_lastpos,recvd_buff);
        buffrx_lastpos = buffrx_lastpos + recvd_buff;
        if(buffrx_lastpos >= state->buffer_rx_len){
            buffrx_lastpos = 0;
            state->buffer_rx_len = 0;
            printf("reset buffRX size\n");
        } 
    }else if(state->buffer_rx_len <= 0){
        // printf("teste9\n");
        return 0;
    }
    // printf("teste10 - %d\n",recvd_buff);    
    if (recvd_buff > size) return -1;
    return recvd_buff;

}

bool socket_impl_listen(struct socket_impl *state){
    err_t err = ERR_ABRT;
    if(state->sock_type == SOCK_TCP){
        if(state->tcp_pcb->state==CLOSED){
            //err = tcp_bind(state->tcp_pcb,state->sock_addr == IPADDR_TYPE_V4 ? IP4_ADDR_ANY : IP6_ADDR_ANY,state->tcp_pcb->remote_port);
            err = tcp_bind(state->tcp_pcb,IP4_ADDR_ANY,state->tcp_pcb->remote_port);
            printf("Listening TCP socket - err: %d\n",err);
            if(err == ERR_OK){
                state->client_status=false;
                tcp_accept(state->tcp_pcb, socket_accept_tcp);
                printf("Client Listening!\n");
                return true;
            } 
        }
    }
    printf("Client Listen Failed!\n");
    return false;
}

bool socket_impl_accept(struct socket_impl *state){
    if(state->client_status && state->sock_type == SOCK_TCP && state->tcp_pcb){
        switch(state->tcp_pcb->state){
            case ESTABLISHED:
                printf("Client Accepted!\n");
                return true;
                break;
            default:
                break;
        }
    }
    printf("Client Waiting...!\n");
    return false;
}
