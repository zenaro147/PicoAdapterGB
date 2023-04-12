#include "picow_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/cyw43_arch.h"

//UDP Callbacks
void socket_recv_udp(void * arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t*addr, u16_t port){
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
}

err_t socket_accept_tcp(void *arg, struct tcp_pcb *pcb, err_t err){
}

err_t socket_send_tcp(char * IP , int port, struct tcp_pcb *pcb, void * data, int data_size){
}

err_t socket_recv_tcp(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err){
}



