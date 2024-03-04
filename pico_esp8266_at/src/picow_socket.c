#include "picow_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/gpio.h"
#include "globals.h"

//UDP Callbacks
void socket_recv_udp(void * arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t*addr, u16_t port){
    struct mobile_user *mobile = (struct mobile_user*)arg;
    struct socket_impl *state = &mobile->socket[mobile->currentReqSocket];
    // printf("UDP Receiving...\n");
    if (p->tot_len > 0) {
        // printf("received UDP from IP: %d.%d.%d.%d port: %d  length: %d\n",
		// addr->addr&0xff,
		// (addr->addr>>8)&0xff,
		// (addr->addr>>16)&0xff,
		// addr->addr>>24,
        // port,
        // p->len);

        memset(state->udp_remote_srv,0x00,sizeof(state->udp_remote_srv));
        sprintf(state->udp_remote_srv, "%d.%d.%d.%d", addr->addr&0xff, (addr->addr>>8)&0xff, (addr->addr>>16)&0xff, addr->addr>>24);
        state->udp_remote_port = port;

        // Receive the buffer
        state->buffer_rx_len = pbuf_copy_partial(p, &state->buffer_rx, p->tot_len, 0);
        int copiedBytes = 0;
        int remainingBytes = p->tot_len;

        while (copiedBytes < p->tot_len) {
            remainingBytes -= copiedBytes;
            int recvsize = pbuf_copy_partial(p, state->buffer_rx, remainingBytes <= 512 ? remainingBytes : 512, copiedBytes);
            copiedBytes += recvsize;
            state->buffer_rx_len = recvsize;
            while (state->buffer_rx_len > 0) {
                mobile_loop(mobile->adapter);
            }
        }
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
    struct mobile_user *mobile = (struct mobile_user*)arg;
    struct socket_impl *state = &mobile->socket[mobile->currentReqSocket];
    printf("TCP Generic Error %d\n", err);
}

err_t socket_accept_tcp(void *arg, struct tcp_pcb *pcb, err_t err){
    struct mobile_user *mobile = (struct mobile_user*)arg;
    struct socket_impl *state = &mobile->socket[mobile->currentReqSocket];

    if (err != ERR_OK || pcb == NULL) {
        // printf("Failure in accept\n");
        return ERR_VAL;
    }
    // printf("Client connected\n");

    state->tcp_pcb = pcb;
    tcp_arg(pcb, arg);
    //tcp_poll(state->tcp_pcb, NULL, 0);
    tcp_accept(pcb, socket_accept_tcp);
    tcp_sent(pcb, socket_sent_tcp);
    tcp_recv(pcb, socket_recv_tcp);
    tcp_err(pcb, socket_err_tcp);

    state->client_status=true;

    return ERR_OK;
}

err_t socket_sent_tcp(void *arg, struct tcp_pcb *pcb, u16_t len){
    struct mobile_user *mobile = (struct mobile_user*)arg;
    struct socket_impl *state = &mobile->socket[mobile->currentReqSocket];
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
    struct mobile_user *mobile = (struct mobile_user*)arg;
    struct socket_impl *state = &mobile->socket[mobile->currentReqSocket];
    // printf("TCP Receiving...\n");
    if(p){
        if (p->tot_len > 0) {
            int copiedBytes = 0;
            int remainingBytes = p->tot_len;
            // uint8_t tmpbuff[BUFF_SIZE] ={0};
            // printf("reading %d bytes\n", p->tot_len);
            // Receive the buffer

            while (copiedBytes < p->tot_len) {
                remainingBytes -= copiedBytes;
                int recvsize = pbuf_copy_partial(p, state->buffer_rx, remainingBytes <= 512 ? remainingBytes : 512, copiedBytes);
                copiedBytes += recvsize;
                state->buffer_rx_len = recvsize;
                while (state->buffer_rx_len > 0) {
                    mobile_loop(mobile->adapter);
                }
            }

            tcp_recved(pcb,copiedBytes);
            // memcpy(state->buffer_rx + state->buffer_rx_len,tmpbuff,copiedBytes);

            if (copiedBytes > 0){
                // printf("received %d bytes\n", copiedBytes);
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