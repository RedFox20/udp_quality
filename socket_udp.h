#ifndef _SOCKET_UDP_H_
#define _SOCKET_UDP_H_

#include <stdio.h>

#if _WIN32
    #include <basetsd.h>
    typedef SSIZE_T ssize_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

int socket_udp_create();
int socket_udp_bind(int socket, const char *bind_address, const char *bind_port);
ssize_t socket_udp_recvfrom(int socket, int flags, char *recv_address, char *recv_port, void *buf, size_t len);
ssize_t socket_udp_sendto(int socket, int flags, char *send_address, char *send_port, const void *buf, size_t len);
int socket_udp_multicast(int socket, char *local_address, char *multicast_address);
int socket_udp_broadcast(int socket);
int socket_udp_local_port(int socket);
void socket_udp_close(int socket);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
