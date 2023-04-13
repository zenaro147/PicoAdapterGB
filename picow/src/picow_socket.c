#include "picow_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "socket_impl.h"
#include "pico/cyw43_arch.h"

//UDP Callbacks
void socket_recv_udp(void * arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t*addr, u16_t port){
    struct socket_impl *state = (struct socket_impl*)arg;
    printf("UDP Receiving...\n");
    if (p->tot_len > 0) {
        printf("received UDP from IP: %d.%d.%d.%d port: %d  length: %d\n",
		addr->addr&0xff,
		(addr->addr>>8)&0xff,
		(addr->addr>>16)&0xff,
		addr->addr>>24,
        port,
        p->len);
        // Receive the buffer
        state->buffer_len = pbuf_copy_partial(p, &state->buffer, p->tot_len > MOBILE_MAX_TRANSFER_SIZE ? MOBILE_MAX_TRANSFER_SIZE : p->tot_len, 0);

        memset(state->udp_remote_srv,0x00,sizeof(state->udp_remote_srv));
        sprintf(state->udp_remote_srv, "%d.%d.%d.%d", addr->addr&0xff, (addr->addr>>8)&0xff, (addr->addr>>16)&0xff, addr->addr>>24);
        state->udp_remote_port = port;
    }
    pbuf_free(p);
}

//TCP Callbacks
err_t socket_connected_tcp(void *arg, struct tcp_pcb *pcb, err_t err) {
    if (err != ERR_OK) {
        printf("connect failed %d\n", err);
    }else{
         printf("TCP connected!\n");
    }
    return err;
}

void socket_err_tcp(void *arg, err_t err){
    struct socket_impl *state = (struct socket_impl*)arg;
}

err_t socket_accept_tcp(void *arg, struct tcp_pcb *pcb, err_t err){
    struct socket_impl *state = (struct socket_impl*)arg;
}

err_t socket_sent_tcp(void *arg, struct tcp_pcb *pcb, u16_t len){
    struct socket_impl *state = (struct socket_impl*)arg;
}

err_t socket_recv_tcp(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err){
    struct socket_impl *state = (struct socket_impl*)arg;
    printf("TCP Receiving...\n");
    cyw43_arch_lwip_check();
    if(p){
        if (p->tot_len > 0) {
            printf("reading %d bytes tot\n", p->tot_len);
            printf("reading %d bytes\n", p->len);
            // Receive the buffer
            state->buffer_len = pbuf_copy_partial(p, &state->buffer, p->tot_len > MOBILE_MAX_TRANSFER_SIZE ? MOBILE_MAX_TRANSFER_SIZE : p->tot_len, 0);
            if (state->buffer_len > 0){
                printf("received %d bytes\n", state->buffer_len);
                err = ERR_OK;
            }else{
                err = ERR_ARG;
            }
            if(state->buffer_len != MOBILE_MAX_TRANSFER_SIZE)
            {
                printf("Free pbuf\n");
                pbuf_free(p);
            } 
        }
         
    }else{
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
        err = ERR_ABRT;
    }    
    return err;
}