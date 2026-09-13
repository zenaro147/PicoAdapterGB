// SPDX-License-Identifier: GPL-3.0-only
#include "picow_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "globals.h"
#include "socket_impl.h"

//UDP Callbacks
void socket_recv_udp(void * arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t*addr, u16_t port){
    // arg is the specific socket_impl this pcb belongs to (bound via
    // udp_recv() in socket_impl_open()) - not resolved through any shared
    // "current connection" index, which would be wrong the moment more than
    // one connection is active (see socket_impl.h's comment on `mobile`).
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

        state->udp_remote_ip[0] = addr->addr & 0xff;
        state->udp_remote_ip[1] = (addr->addr >> 8) & 0xff;
        state->udp_remote_ip[2] = (addr->addr >> 16) & 0xff;
        state->udp_remote_ip[3] = (addr->addr >> 24) & 0xff;
        state->udp_remote_port = port;

        // One datagram, one message: copy what buffer_rx can hold and discard
        // the rest, rather than looping over the pbuf and handing libmobile
        // each slice as though it were a separate packet - mobile.h's
        // sock_recv is explicit that an oversized datagram must be truncated
        // with the remainder dropped, since each result is parsed as one
        // complete message. The clamp is also what keeps a datagram larger
        // than BUFF_SIZE (they can reach the link MTU, and beyond it with IP
        // reassembly) from overflowing buffer_rx into the rest of this
        // socket_impl and the next entry of socket_storage[].
        // libmobile only ever opens UDP sockets for DNS, and asks for
        // MOBILE_DNS_PACKET_SIZE (512) - which is what this buffer is sized
        // to hold.
        int recvsize = pbuf_copy_partial(p, state->buffer_rx, p->tot_len <= 512 ? p->tot_len : 512, 0);
        state->buffer_rx_len = recvsize;
        while (state->buffer_rx_len > 0) {
            mobile_loop(state->mobile->adapter);
        }
    }
    pbuf_free(p);
}

//TCP Callbacks
err_t socket_connected_tcp(void *arg, struct tcp_pcb *pcb, err_t err) {
    if (err != ERR_OK) {
        // printf("connect failed %d\n", err);
    }else{
        // printf("TCP connected!\n");
    }
    return err;
}

void socket_err_tcp(void *arg, err_t err){
    // See socket_recv_udp()'s comment: arg is this pcb's own socket_impl.
    struct socket_impl *state = (struct socket_impl*)arg;
    // lwIP has already freed the pcb by the time tcp_err() fires (see
    // tcp.h's tcp_err_fn doc: "the pcb has been closed"; that's also why
    // this callback, unlike tcp_sent/tcp_recv/tcp_accept, isn't even given
    // the pcb pointer). Clear it here so nothing downstream dereferences a
    // dangling pointer (socket_impl_connect()/_send()/_recv() all key off
    // tcp_pcb to decide what to do next) - a real Mobile Adapter P2P call
    // is exactly where this matters most: relay/internet connections tend
    // to get closed by us first, but a P2P peer can RST or abandon a
    // connect attempt at any time, and libmobile keeps polling this same
    // socket afterward instead of tearing it down immediately.
    state->tcp_pcb = NULL;
    state->socket_status = err;
    DEBUG_PRINT_FUNCTION("TCP Generic Error %d", err);
}

err_t socket_accept_tcp(void *arg, struct tcp_pcb *pcb, err_t err){
    // arg is the listening socket_impl's own state (tcp_arg() set this in
    // socket_impl_listen()); re-registering the same arg on the accepted
    // pcb below correctly carries it over to the live connection.
    struct socket_impl *state = (struct socket_impl*)arg;

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
    state->pending_close = true;
    if(p){
        if (p->tot_len > 0) {
            int copiedBytes = 0;
            // uint8_t tmpbuff[BUFF_SIZE] ={0};
            // printf("reading %d bytes\n", p->tot_len);
            // Receive the buffer

            while (copiedBytes < p->tot_len) {
                // See socket_recv_udp(): recomputed, not decremented by the
                // running total, or it goes negative from the third chunk on
                // and the 512 clamp below stops applying.
                int remainingBytes = p->tot_len - copiedBytes;
                int recvsize = pbuf_copy_partial(p, state->buffer_rx, remainingBytes <= 512 ? remainingBytes : 512, copiedBytes);
                copiedBytes += recvsize;
                state->buffer_rx_len = recvsize;
                while (state->buffer_rx_len > 0) {
                    mobile_loop(state->mobile->adapter);
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
        // Use `pcb` - this callback invocation's own, always-correct
        // pointer - not state->tcp_pcb. By the time a peer's FIN actually
        // arrives here, our cached state->tcp_pcb may already have moved on
        // (nulled by socket_impl_close(), or reassigned to a brand new pcb
        // by socket_impl_connect()'s P2P retry) while lwIP is still
        // delivering this now-stale connection's own final callback for
        // *this* pcb specifically. Operating on state->tcp_pcb here instead
        // could hand tcp_close()/tcp_abort() a NULL pointer (confirmed on
        // hardware: lwIP's own "tcp_close: invalid pcb"/"tcp_abandon:
        // invalid pcb" assertions, then a hardfault inside tcp_arg()) or,
        // worse, silently abort a *different*, currently-live connection
        // that had since reused the same socket_impl slot - a real "ghost
        // socket" bug, not just a crash.
        err_t close_err = tcp_close(pcb);
        if (close_err != ERR_OK) {
            // printf("close failed %d, calling abort\n", close_err);
            tcp_abort(pcb);
            err = ERR_ABRT;
        }
        if (state->tcp_pcb == pcb) state->tcp_pcb = NULL;
    }
    state->pending_close = false;
    return err;
}
