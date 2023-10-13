#include "simple_udp.h"
#include "logging.h"
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
    #include <poll.h>
    #include <fcntl.h>
    #include <sys/ioctl.h>
#endif

#if _WIN32
static WSADATA wsaInit;
#endif

int socket_udp_create() noexcept
{
#if _WIN32
    if (wsaInit.wVersion == 0)
        WSAStartup(MAKEWORD(2, 2), &wsaInit);
#endif
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

int socket_udp_listener(int socket, int local_port) noexcept
 {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(local_port);
    int t = bind(socket, (struct sockaddr *)&addr, sizeof(addr));
    if (t < 0) {
        socket_udp_close(socket);
        return t;
    }
    return 0;
}

void socket_udp_close(int socket) noexcept
{
#if __linux__
    shutdown(socket, SHUT_RDWR);
    close(socket);
#else
    shutdown(socket, SD_BOTH);
    closesocket(socket);
#endif
}

void socket_set_blocking(int socket, bool is_blocking) noexcept
{
    #if _WIN32
        u_long val = is_blocking?0:1; // FIONBIO: !=0 nonblock, 0 block
        if (ioctlsocket(socket, FIONBIO, &val) != 0)
            LogErrorExit("Error setting socket to nonblocking");
    #else
        int flags = fcntl(socket, F_GETFL, 0);
        if (flags < 0) flags = 0;
        flags = is_blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
        if (fcntl(socket, F_SETFL, flags) != 0)
            LogErrorExit("Error setting socket to nonblocking");
    #endif
}


void socket_set_buf_size(int socket, bool rcv_buf, int buf_size) noexcept
{
    int so_buf = (rcv_buf ? SO_RCVBUF : SO_SNDBUF);
#if __linux__
    // NOTE: on linux the kernel doubles buffsize for internal bookkeeping
    //       so to keep things consistent between platforms, we divide by 2 on linux:
    int size_cmd = static_cast<int>(buf_size / 2);
    int so_buf_force = (so_buf == SO_RCVBUF ? SO_RCVBUFFORCE : SO_SNDBUFFORCE);
#else
    int size_cmd = static_cast<int>(buf_size);
    int so_buf_force = 0;
#endif
    bool ok = setsockopt(socket, SOL_SOCKET, so_buf, (char*)&size_cmd, sizeof(int)) == 0;
    if (!ok && so_buf_force != 0)
        setsockopt(socket, SOL_SOCKET, so_buf_force, (char*)&size_cmd, sizeof(int));
}

int socket_get_buf_size(int socket, bool rcv_buf) noexcept
{
    int so_buf = (rcv_buf ? SO_RCVBUF : SO_SNDBUF);
    int buf_size = 0;
    socklen_t len = sizeof(int);
    getsockopt(socket, SOL_SOCKET, so_buf, (char*)&buf_size, &len);
    return buf_size;
}

int socket_sendto(int socket, const void* data, int size, uint32_t sin_addr, unsigned short port) noexcept
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = sin_addr;
    addr.sin_port        = htons(port);
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
    return sendto(socket, (const char*)data, size, 0, (struct sockaddr*)&addr, sizeof(addr));
}

int socket_recvfrom(int socket, void* buffer, int maxsize, unsigned long* sin_addr, unsigned short* port) noexcept
{
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int size = recvfrom(socket, (char*)buffer, maxsize, 0, (struct sockaddr*)&addr, &addr_len);
    if (size > 0) {
        if (sin_addr) *sin_addr = addr.sin_addr.s_addr;
        if (port)     *port     = ntohs(addr.sin_port);
    }
    return size;
}

bool socket_poll_recv(int socket, int timeout_ms) noexcept
{
    struct pollfd pfd;
    pfd.fd = socket;
    pfd.events = POLLIN;
    pfd.revents = 0;
#if _WIN32 || _WIN64
    int r = WSAPoll(&pfd, 1, timeout_ms);
#else
    int r = ::poll(&pfd, 1, timeout_ms);
#endif
    if (r <= 0)
        return false; // no data available (timeout)
    return (pfd.revents & POLLIN) != 0;
}
