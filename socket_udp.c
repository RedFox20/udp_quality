#include "socket_udp.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h> // ssize_t


#if _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <WinSock2.h>
    #include <ws2tcpip.h>           // winsock2 and TCP/IP functions
    #include <mstcpip.h>            // WSAIoctl Options
    #ifdef _MSC_VER
        #pragma comment(lib, "Ws2_32.lib") // link against winsock libraries
        #pragma comment(lib, "Iphlpapi.lib")
    #endif
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
#endif


int socket_udp_create() {
    int s = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) return s;
    int t = 1;
    t = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&t, sizeof(t));
    if (t < 0) {
        socket_udp_close(s);
        return t;
    }
    return s;
}

int socket_udp_bind(int socket, const char *bind_address, const char *bind_port){
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = bind_address ? inet_addr(bind_address) : INADDR_ANY;
    addr.sin_port        = bind_port    ? htons(atoi(bind_port))  : htons(0);

    int t = bind(socket, (struct sockaddr *)&addr, sizeof(addr));
    if (t < 0) {
        socket_udp_close(socket);
        return t;
    }
    return 0;
}

ssize_t socket_udp_recvfrom(int socket, int flags, char *recv_address, char *recv_port, void *buf, size_t len){
    struct sockaddr addr;
    ssize_t l;
    int t;

    t = sizeof(addr);
    l = recvfrom(socket, buf, len, flags, &addr, (socklen_t *)&t);
    if(l < 0)return(l);
    if(recv_address != NULL){
        sprintf(recv_address, "%u.%u.%u.%u", (unsigned int)((uint8_t)addr.sa_data[2]), (unsigned int)((uint8_t)addr.sa_data[3]), (unsigned int)((uint8_t)addr.sa_data[4]), (unsigned int)((uint8_t)addr.sa_data[5]));
    }
    if(recv_port != NULL){
        sprintf(recv_port, "%u", (unsigned int)(((uint16_t)((uint8_t)addr.sa_data[0]) << 8) | (uint8_t)addr.sa_data[1]));
    }
    return(l);
}

ssize_t socket_udp_sendto(int socket, int flags, char *send_address, char *send_port, const void *buf, size_t len){
    struct addrinfo hints, *result;
    ssize_t l;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if(getaddrinfo(send_address, send_port, &hints, &result))return(-1);

    l = sendto(socket, buf, len, flags, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);
    return(l);
}

int socket_udp_multicast(int socket, char *local_address, char *multicast_address){
    struct ip_mreq mreq;

    memset(&mreq, 0, sizeof(mreq));
    if(local_address != NULL){
        mreq.imr_interface.s_addr = inet_addr(local_address);
    }else{
        mreq.imr_interface.s_addr = INADDR_ANY;
    }
    mreq.imr_multiaddr.s_addr = inet_addr(multicast_address);
    return(setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char*)&mreq, sizeof(mreq)));
}

int socket_udp_broadcast(int socket){
    int t;

    t = 1;
    return(setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (const char*)&t, sizeof(t)));
}

int socket_udp_local_port(int socket){
    struct sockaddr_in addr;
    socklen_t l;
    l = sizeof(addr);
    if(getsockname(socket, (struct sockaddr *)&addr, &l) < 0)return(-1);
    return(ntohs(addr.sin_port));
}

void socket_udp_close(int socket){
    #if __linux__
        shutdown(socket, SHUT_RDWR);
    #else
        shutdown(socket, SD_BOTH);
    #endif
    close(socket);
}
