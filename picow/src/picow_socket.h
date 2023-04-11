#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"

#include <mobile.h>

struct picow_sock_config {
    int sock_id;
    uint8_t sock_type;
    unsigned char buffer[MOBILE_MAX_TRANSFER_SIZE];
    int buffer_len;
	union{
		struct tcp_pcb *tcp_pcb;
        struct udp_pcb *udp_pcb;
	};
};

// UDP functions
void socket_new_udp(void *arg);
void socket_bind_udp(void *arg);
err_t socket_send_udp(char * IP , int port, struct udp_pcb *pcb, void * data, int data_size);
void socket_recv_udp(void * arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t*addr, u16_t port);
void socket_close_udp(void *arg);

// TCP Functions
void socket_new_tcp(void *arg);
void socket_bind_tcp(void *arg);
err_t socket_connect_tcp(char * IP , int port, struct tcp_pcb *pcb, void * data, int data_size);
err_t socket_connected_tcp(void *arg, struct tcp_pcb *tpcb, err_t err);
err_t socket_poll_tcp(void *arg, struct tcp_pcb *tpcb);
void socket_err_tcp(void *arg, err_t err);
err_t socket_accept_tcp(void *arg, struct tcp_pcb *client_pcb, err_t err);
err_t socket_send_tcp(char * IP , int port, struct tcp_pcb *pcb, void * data, int data_size);
err_t socket_sent_tcp(void *arg, struct tcp_pcb *tpcb, u16_t len);
err_t socket_recv_tcp(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
err_t socket_close_tcp(void *arg);

//Generic Calls to redirect
