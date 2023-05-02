#include "picow_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "socket_impl.h"
#include "pico/cyw43_arch.h"

//UDP Callbacks
void socket_recv_udp(void * arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t*addr, u16_t port){
    struct socket_impl *state = (struct socket_impl*)arg;
    // printf("UDP Receiving...\n");
    if (p->tot_len > 0) {
        // printf("received UDP from IP: %d.%d.%d.%d port: %d  length: %d\n",
		// addr->addr&0xff,
		// (addr->addr>>8)&0xff,
		// (addr->addr>>16)&0xff,
		// addr->addr>>24,
        // port,
        // p->len);

        // Receive the buffer
        state->buffer_rx_len = pbuf_copy_partial(p, &state->buffer_rx, p->tot_len > BUFF_SIZE ? BUFF_SIZE : p->tot_len, 0);
        memset(state->udp_remote_srv,0x00,sizeof(state->udp_remote_srv));
        sprintf(state->udp_remote_srv, "%d.%d.%d.%d", addr->addr&0xff, (addr->addr>>8)&0xff, (addr->addr>>16)&0xff, addr->addr>>24);
        state->udp_remote_port = port;
    }
    pbuf_free(p);
}

//TCP Callbacks
err_t socket_connected_tcp(void *arg, struct tcp_pcb *pcb, err_t err) {
    if (err != ERR_OK) {
        // printf("connect failed %d\n", err);
    }else{
        //  printf("TCP connected!\n");
    }
    return err;
}

void socket_err_tcp(void *arg, err_t err){
    struct socket_impl *state = (struct socket_impl*)arg;
    printf("TCP Generic Error %d\n", err);
}

err_t socket_accept_tcp(void *arg, struct tcp_pcb *pcb, err_t err){
    struct socket_impl *state = (struct socket_impl*)arg;

    if (err != ERR_OK || pcb == NULL) {
        // printf("Failure in accept\n");
        return ERR_VAL;
    }
    // printf("Client connected\n");

    state->tcp_pcb = pcb;
    tcp_arg(pcb, state);
    //tcp_poll(state->tcp_pcb, NULL, 0);
    tcp_accept(pcb, socket_accept_tcp);
    tcp_sent(pcb, socket_sent_tcp);
    tcp_recv(pcb, socket_recv_tcp);
    tcp_err(pcb, socket_err_tcp);

    state->client_status=true;

    return ERR_OK;
}

err_t socket_sent_tcp(void *arg, struct tcp_pcb *pcb, u16_t len){
    struct socket_impl *state = (struct socket_impl*)arg;
    err_t err = ERR_ABRT;
    if(state->buffer_tx_len != len){
        // printf("TCP sent %d bytes to: %s:%d. But should sent %d\n",len,ip4addr_ntoa(&pcb->remote_ip),pcb->remote_port,state->buffer_tx_len);
        state->buffer_tx_len = len;
        err = ERR_BUF;
    }else{
        // printf("TCP sent %d bytes to IP: %s:%d.\n",len,ip4addr_ntoa(&pcb->remote_ip),pcb->remote_port);
        err = ERR_OK;
    }
    return err;
}

err_t socket_recv_tcp(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err){
    struct socket_impl *state = (struct socket_impl*)arg;
    // printf("TCP Receiving...\n");
    if(p){
        if (p->tot_len > 0) {
            int recvsize = 0;
            uint8_t tmpbuff[BUFF_SIZE] ={0};
            // printf("reading %d bytes\n", p->tot_len);
            // Receive the buffer
            recvsize = pbuf_copy_partial(p, &tmpbuff, p->tot_len, 0);
            tcp_recved(pcb,recvsize);
            memcpy(state->buffer_rx + state->buffer_rx_len,tmpbuff,recvsize);
            state->buffer_rx_len = state->buffer_rx_len + recvsize;
            if (recvsize > 0){
                // printf("received %d bytes\n", recvsize);
                err = ERR_OK;
            }else{
                err = ERR_BUF;
            }
            pbuf_free(p);
        }
    }else{
        err = tcp_close(state->tcp_pcb);
        if (err != ERR_OK) {
            // printf("close failed %d, calling abort\n", err);
            tcp_abort(state->tcp_pcb);
            err = ERR_ABRT;
        }
        tcp_arg(state->tcp_pcb, NULL);
        //tcp_poll(state->tcp_pcb, NULL, 0);
        tcp_accept(state->tcp_pcb, NULL);
        tcp_sent(state->tcp_pcb, NULL);
        tcp_recv(state->tcp_pcb, NULL);
        tcp_err(state->tcp_pcb, NULL);
        state->tcp_pcb = 0x0;
        state->tcp_pcb = NULL;
    }
    return err;
}