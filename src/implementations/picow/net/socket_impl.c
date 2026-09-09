#include "socket_impl.h"
#include "picow_socket.h"
#include "globals.h"

#include "pico/cyw43_arch.h"

#include <string.h>

// How long socket_impl_connect() used to keep silently retrying a P2P TCP
// connect after an immediate refusal (RST) before finally reporting failure.
// Currently unused - see the TEMPORARILY DISABLED comment in
// socket_impl_connect() itself for why. Left defined so the value isn't
// lost if this is reinstated (scoped to real P2P connections, once
// dependences/libmobile can signal that) rather than removed entirely.
#define TCP_CONNECT_RETRY_WINDOW_MS 20000

// Static storage for every connection's handle: MOBILE_MAX_CONNECTIONS is a
// small libmobile-defined constant (currently 2), so this avoids heap
// allocation/fragmentation for state that lives for the whole program.
static struct socket_impl socket_storage[MOBILE_MAX_CONNECTIONS];

void socket_hal_bind(struct mobile_user *mobile){
    for (int i = 0; i < MOBILE_MAX_CONNECTIONS; i++){
        socket_storage[i].mobile = mobile;
        mobile->socket[i] = &socket_storage[i];
    }
}

void socket_hal_reset(struct socket_impl *state){
    state->tcp_pcb = NULL; // clears the tcp_pcb/udp_pcb union
    state->sock_addr = -1;
    state->sock_type = SOCK_NONE;
    memset(state->udp_remote_ip, 0x00, sizeof(state->udp_remote_ip));
    state->udp_remote_port = 0;
    state->client_status = false;
    state->inside_callback = false;
    state->pending_close = false;
    state->socket_status = 0;
    memset(state->buffer_rx, 0x00, sizeof(state->buffer_rx));
    state->buffer_rx_len = 0;
    state->buffer_tx_len = 0;
    state->buffer_rx_read_pos = 0;
}

bool socket_impl_open(struct socket_impl *state, enum mobile_socktype socktype, enum mobile_addrtype addrtype, unsigned bindport, void *user){
    (void)user; // lwIP callbacks are bound to `state` directly below, not to `user` (see socket_impl.h)

    if (state->tcp_pcb != NULL || state->udp_pcb != NULL) return false;

    state->buffer_rx_read_pos = 0;
    state->connect_deadline_us = 0;

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
    // printf("Socket Open.\n");
    return true;
}

void socket_impl_close(struct socket_impl *state){
    if (state->inside_callback) {
        state->pending_close = true;
        return;
    }
    err_t err = ERR_ARG;
    switch (state->sock_type) {
        case SOCK_TCP:
            if(state->tcp_pcb){
                // arg is deliberately NOT cleared here (tcp_recv/tcp_sent/
                // tcp_err below are also deliberately left registered,
                // hence commented out rather than removed): tcp_close()
                // only starts a graceful close - lwIP keeps this pcb alive
                // internally (FIN_WAIT/CLOSING/TIME_WAIT) and can still
                // invoke those callbacks after this function returns, once
                // our own state->tcp_pcb below has already gone back to
                // NULL. Nulling arg here previously left a live registered
                // callback with arg==NULL, which every callback in
                // picow_socket.c dereferences unconditionally - the next
                // stray callback for this closing connection (observed on
                // hardware right at the end of a P2P call) crashed with a
                // NULL-pointer hardfault instead of harmlessly finding
                // arg's mobile_user still valid.
                // tcp_poll(state->tcp_pcb, NULL, 0);
                // tcp_accept(state->tcp_pcb, NULL);
                // tcp_sent(state->tcp_pcb, NULL);
                // tcp_recv(state->tcp_pcb, NULL);
                // tcp_err(state->tcp_pcb, NULL);
                err = tcp_close(state->tcp_pcb);
                if (err != ERR_OK) {
                    DEBUG_PRINT_FUNCTION("Socket close failed %d, calling abort", err);
                    tcp_abort(state->tcp_pcb);
                }else{
                    DEBUG_PRINT_FUNCTION("Socket Closed.");
                }
                state->tcp_pcb = NULL;
            }
            break;
        case SOCK_UDP:
            // Order matters: udp_remove() ends in memp_free(), so the pcb is
            // gone once it returns - calling udp_recv()/udp_disconnect()
            // after it wrote into freed pool memory (they both dereference
            // pcb unconditionally). Unregister the callback and drop the
            // remote peer first, free last.
            udp_recv(state->udp_pcb, NULL, NULL);
            udp_disconnect(state->udp_pcb);
            udp_remove(state->udp_pcb);
            state->udp_pcb = NULL;
            break;
        default: 
            break;
    }
    state->sock_addr = -1;
    state->sock_type = SOCK_NONE;
    memset(state->udp_remote_ip,0x00,sizeof(state->udp_remote_ip));
    state->udp_remote_port = 0;
    state->client_status = false;
    state->inside_callback = false;
    state->pending_close = false;
    state->socket_status = 0;
    memset(state->buffer_rx,0x00,sizeof(state->buffer_rx));
    //memset(state->buffer_tx,0x00,sizeof(state->buffer_tx));
    state->buffer_rx_len = 0;
    state->buffer_tx_len = 0;
    state->buffer_rx_read_pos = 0;
    // printf("Socket Closed.\n");
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
    if (state->sock_type == SOCK_TCP && state->tcp_pcb == NULL) {
        // The pcb was already freed and cleared by socket_err_tcp() (a
        // failed connect attempt - e.g. the peer isn't listening yet and
        // TCP replies with an immediate RST - is reported through tcp_err,
        // not socket_connected_tcp - see its comment).
        //
        // TEMPORARILY DISABLED (see TCP_CONNECT_RETRY_WINDOW_MS's own
        // comment above): this used to silently reopen a fresh pcb and
        // retry here for up to TCP_CONNECT_RETRY_WINDOW_MS, to mirror a
        // real Mobile Adapter P2P call not getting an instant "unreachable"
        // on one failed ring. Pulled out while investigating a 2026-09
        // hardware report of a generic (non-P2P) TCP relay connect hanging
        // for ~20s before the whole session timed out: `conn` here is
        // shared between P2P (always dependences/libmobile's hardcoded
        // p2p_conn == 0) and generic TCP_CONNECT (connection_new() picks
        // the first free slot, which is *also* 0 whenever P2P isn't
        // active) - there's no reliable signal at this layer to scope the
        // retry to P2P only, so it was removed here entirely to test
        // whether it explains the hang, rather than guess with a heuristic
        // that could just as easily apply to the wrong case either way.
        // If this turns out unrelated, and P2P connects need the grace
        // period back, that requires libmobile exposing a real "this is
        // P2P" signal through the callback rather than reintroducing this
        // guess.
        state->connect_deadline_us = 0;
        state->socket_status = 0;
        return -1;
    } else if(state->sock_type == SOCK_TCP && state->tcp_pcb->state != CLOSED){
        if (state->socket_status < 0){
            state->socket_status = 0;
            return -1;
        }
        switch (state->tcp_pcb->state){
            case ESTABLISHED:
                state->connect_deadline_us = 0;
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
                // printf("Connect TCP failed %d\n", err);
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
                // printf("Connect TCP failed %d\n", err);
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
    // The address-family check has to stay *inside* the `addr &&` guard.
    // It used to sit outside it - the guard only covered the IPv4 arm, so
    // the IPv6 arm dereferenced addr unconditionally - and addr is NULL for
    // every TCP send (see mobile.h's sock_send). It never showed up because
    // on RP2040 address 0 is the bootrom: the read returns a byte that
    // happens not to be MOBILE_ADDRTYPE_IPV6 instead of faulting. Nothing
    // guarantees that on RP2350.
    if(
        (state->sock_type == SOCK_TCP && (!state->tcp_pcb || state->tcp_pcb->state != ESTABLISHED)) ||
        (state->sock_type == SOCK_UDP && !state->udp_pcb && !addr) ||
        (addr && ((addr->type == MOBILE_ADDRTYPE_IPV4 && state->sock_addr == IPADDR_TYPE_V6) ||
                  (addr->type == MOBILE_ADDRTYPE_IPV6 && state->sock_addr == IPADDR_TYPE_V4)))){
        return -1;
    }

    state->buffer_tx_len = 0;

    err_t err = ERR_ARG;
    if(state->sock_type == SOCK_TCP){
        cyw43_arch_lwip_begin();
        err = tcp_write(state->tcp_pcb,data,size,TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK) {
            state->buffer_tx_len = size;
            // tcp_write() only *enqueues*; it never calls tcp_output()
            // itself (verified in the SDK's tcp_out.c). Without this, the
            // segment waits for whatever else happens to flush the pcb -
            // and for a request/response protocol nothing does, because the
            // peer cannot reply to a request it has not received yet, so it
            // falls to a retransmission-timer path measured in hundreds of
            // milliseconds. Every send here is one whole Mobile Adapter
            // transfer, so that delay lands once per transfer. The two other
            // lwIP users in this firmware (web_http.c, picow_device_auth_http.c)
            // already call it; this path was the one that didn't.
            // Its return is deliberately ignored: the data is queued either
            // way, and ERR_MEM here only means "couldn't push it out this
            // instant", which is not a send failure.
            tcp_output(state->tcp_pcb);
        }
        cyw43_arch_lwip_end();
    }else if(state->sock_type == SOCK_UDP) {
        //Set a new IP/Port if receive an addr parameter
        if(addr){
            char srv_ip[46];
            memset(srv_ip,0x00,sizeof(srv_ip));
            if (addr->type == MOBILE_ADDRTYPE_IPV4) {
                const struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
                sprintf(srv_ip, "%u.%u.%u.%u", addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3]);
                ip4addr_aton(srv_ip, &state->udp_pcb->remote_ip);
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
        
        struct pbuf * p = pbuf_alloc(PBUF_TRANSPORT,size,PBUF_RAM);
        if(!p){
            // Out of pbufs at this instant - transient, exactly like the
            // ERR_MEM case below, so report "sent nothing yet" rather than a
            // hard error. Unchecked, `p->payload` read a word of the
            // bootrom (RP2040 maps it at address 0, so no fault to catch the
            // mistake) and the memcpy below wrote the caller's data through
            // that word as if it were a pointer.
            return 0;
        }
        uint8_t *pt = (uint8_t *) p->payload;
        memcpy(pt,data,size);
        
        cyw43_arch_lwip_begin();
        err = udp_send(state->udp_pcb,p);
        if (err == ERR_OK) state->buffer_tx_len = size;
        cyw43_arch_lwip_end();
        pbuf_free(p);
    }else{
        return -1;
    }
    if(err == ERR_MEM){
        // Backpressure, not failure. lwIP's own tcp_write() doc says it
        // returns ERR_MEM when the data exceeds the send buffer or the
        // segment queue is full, that it does "not change anything in pcb"
        // in that case (so nothing was queued), and that the application
        // should wait for the peer to ack and try again. mobile.h's
        // sock_send is non-blocking and "will be called repeatedly until all
        // of the data is sent", with 0 meaning no bytes went out this time.
        // Returning -1 here instead reported a fatal socket error, and
        // libmobile bails out of the whole transfer on rc < 0 (relay.c:98,
        // pop3_auth.c:96) - killing a connection over a condition that
        // clears itself on the next poll.
        return 0;
    }
    if(err != ERR_OK){
        // printf("Send failed %d\n", err);
        return -1;
    }

    // cyw43_arch_poll();

    return state->buffer_tx_len;
}

int socket_impl_recv(struct socket_impl *state, void *data, unsigned size, struct mobile_addr *addr){
    // "Is the connection still alive?" check: must run before touching the
    // rx buffer, regardless of whether data is still pending, otherwise this
    // would fall through to memcpy(NULL, ...) below.
    if(!data){
        if(state->sock_type != SOCK_TCP) return 0;
        // PCB already closed by socket_recv_tcp (remote FIN received)
        if (state->tcp_pcb == NULL) {
            return -2;
        }
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

    //If the socket is a TCP and don't have any buff, check if it's disconnected to return an error
    if(state->sock_type == SOCK_TCP && state->buffer_rx_len <= 0){
        if((!state->tcp_pcb || state->tcp_pcb->state == CLOSED)){
            return -2;
        }     
    }

    // cyw43_arch_poll();

    int recvd_buff = 0;
    if(state->buffer_rx_len > 0){
        if (addr && state->sock_type == SOCK_UDP){
            struct mobile_addr4 *addr4 = (struct mobile_addr4 *)addr;
            addr4->type = MOBILE_ADDRTYPE_IPV4;
            addr4->port = state->udp_remote_port;
            memcpy(addr4->host, state->udp_remote_ip, sizeof(addr4->host));
        }
        
        uint16_t tmpsize = state->buffer_rx_len - state->buffer_rx_read_pos;
        if(state->sock_type == SOCK_UDP){
            // A datagram is one whole message, not a stream. mobile.h's
            // sock_recv requires an oversized one to be truncated with the
            // remainder discarded, because libmobile parses each result as a
            // complete message - slicing it the way the TCP branch does
            // would hand it the first MOBILE_MAX_TRANSFER_SIZE bytes of a
            // DNS response as if that were the entire packet.
            recvd_buff = tmpsize > size ? size : tmpsize;
        }else{
            // TCP has no message boundaries, so slicing is both correct and
            // required here: libmobile reads at most MOBILE_MAX_TRANSFER_SIZE
            // per call and keeps calling until the stream is drained.
            if(tmpsize > MOBILE_MAX_TRANSFER_SIZE){
                recvd_buff = MOBILE_MAX_TRANSFER_SIZE;
            }else{
                recvd_buff = tmpsize;
            }
            if (recvd_buff > size) recvd_buff = size;
        }

        // printf("copied %d bytes\n",recvd_buff);
        memcpy(data,state->buffer_rx + state->buffer_rx_read_pos,recvd_buff);
        state->buffer_rx_read_pos = state->buffer_rx_read_pos + recvd_buff;
        // UDP always consumes the whole datagram, even when it didn't fit:
        // whatever was truncated is discarded, never held back for a second
        // call that would look like a new packet.
        if(state->sock_type == SOCK_UDP || state->buffer_rx_read_pos >= state->buffer_rx_len){
            state->buffer_rx_read_pos = 0;
            state->buffer_rx_len = 0;
        }
    }else if(state->buffer_rx_len <= 0){
        return 0;
    }  
    if (recvd_buff > size) return -1;
    return recvd_buff;

}

bool socket_impl_listen(struct socket_impl *state, void *user){
    (void)user; // lwIP callbacks are bound to `state` directly (see socket_impl.h)
    err_t err = ERR_ABRT;
    if(state->sock_type == SOCK_TCP){
        if(state->tcp_pcb->state==CLOSED){
            // err = tcp_bind(state->tcp_pcb,state->sock_addr == IPADDR_TYPE_V4 ? IP4_ADDR_ANY : IP6_ADDR_ANY,state->tcp_pcb->remote_port);
            err = tcp_bind(state->tcp_pcb,IP4_ADDR_ANY,state->tcp_pcb->local_port);
            // printf("Listening TCP socket - err: %d\n",err);
            if(err == ERR_OK){
                state->client_status=false;
                state->tcp_pcb = tcp_listen_with_backlog(state->tcp_pcb,1);
                tcp_arg(state->tcp_pcb, state);
                tcp_accept(state->tcp_pcb, socket_accept_tcp);
                // printf("Client Listening!\n");
                return true;
            } 
        }
    }
    // printf("Client Listen Failed!\n");
    return false;
}

bool socket_impl_accept(struct socket_impl *state){
    // cyw43_arch_poll();
    if(state->client_status && state->sock_type == SOCK_TCP && state->tcp_pcb){
        switch(state->tcp_pcb->state){
            case ESTABLISHED:
                // printf("Client Accepted!\n");
                return true;
                break;
            default:
                break;
        }
    }
    // printf("Client Waiting...!\n");
    return false;
}

void socket_impl_close_commands(struct socket_impl *state){
    err_t err = ERR_ARG;
    switch (state->sock_type) {
        case SOCK_TCP:
            if(state->tcp_pcb){
                // arg is deliberately NOT cleared here (tcp_recv/tcp_sent/
                // tcp_err below are also deliberately left registered,
                // hence commented out rather than removed): tcp_close()
                // only starts a graceful close - lwIP keeps this pcb alive
                // internally (FIN_WAIT/CLOSING/TIME_WAIT) and can still
                // invoke those callbacks after this function returns, once
                // our own state->tcp_pcb below has already gone back to
                // NULL. Nulling arg here previously left a live registered
                // callback with arg==NULL, which every callback in
                // picow_socket.c dereferences unconditionally - the next
                // stray callback for this closing connection (observed on
                // hardware right at the end of a P2P call) crashed with a
                // NULL-pointer hardfault instead of harmlessly finding
                // arg's mobile_user still valid.
                // tcp_poll(state->tcp_pcb, NULL, 0);
                // tcp_accept(state->tcp_pcb, NULL);
                // tcp_sent(state->tcp_pcb, NULL);
                // tcp_recv(state->tcp_pcb, NULL);
                // tcp_err(state->tcp_pcb, NULL);
                err = tcp_close(state->tcp_pcb);
                if (err != ERR_OK) {
                     DEBUG_PRINT_FUNCTION("Socket close failed %d, calling abort", err);
                    tcp_abort(state->tcp_pcb);
                }else{
                    DEBUG_PRINT_FUNCTION("Socket Closed.");
                }
                state->tcp_pcb = NULL;
            }
            break;
        case SOCK_UDP:
            // Order matters: udp_remove() ends in memp_free(), so the pcb is
            // gone once it returns - calling udp_recv()/udp_disconnect()
            // after it wrote into freed pool memory (they both dereference
            // pcb unconditionally). Unregister the callback and drop the
            // remote peer first, free last.
            udp_recv(state->udp_pcb, NULL, NULL);
            udp_disconnect(state->udp_pcb);
            udp_remove(state->udp_pcb);
            state->udp_pcb = NULL;
            break;
        default: 
            break;
    }
    state->sock_addr = -1;
    state->sock_type = SOCK_NONE;
    memset(state->udp_remote_ip,0x00,sizeof(state->udp_remote_ip));
    state->udp_remote_port = 0;
    state->client_status = false;
    state->inside_callback = false;
    state->pending_close = false;
    state->socket_status = 0;
    memset(state->buffer_rx,0x00,sizeof(state->buffer_rx));
    //memset(state->buffer_tx,0x00,sizeof(state->buffer_tx));
    state->buffer_rx_len = 0;
    state->buffer_tx_len = 0;
    state->buffer_rx_read_pos = 0;
    // printf("Socket Closed.\n");
}
