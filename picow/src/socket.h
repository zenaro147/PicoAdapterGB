// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "common.h"

#include <errno.h>
#include <unistd.h>

#include "lwip/sockets.h"
//#include <netinet/in.h>
#include "lwip/tcp.h"
#include "lwip/udp.h"

#define socket_close close
#define socket_geterror() errno
#define socket_seterror(e) errno = (e)
#define SOCKET_EWOULDBLOCK EWOULDBLOCK
#define SOCKET_EINPROGRESS EINPROGRESS
#define SOCKET_EALREADY EALREADY

void socket_perror(const char *func);
int socket_straddr(char *res, unsigned res_len, char *res_port, struct sockaddr *addr, socklen_t addrlen);
int socket_hasdata(int socket, int delay);
int socket_isconnected(int socket, int delay);
int socket_setblocking(int socket, int flag);
int socket_connect(const char *host, const char *port);
