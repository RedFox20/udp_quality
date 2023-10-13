#pragma once
#include <stdint.h>

// a much simpler socket interface to eliminate library errors
// however the default implementation is still rpp::socket

int socket_udp_create() noexcept;
int socket_udp_listener(int socket, int local_port) noexcept;
void socket_udp_close(int socket) noexcept;
void socket_set_blocking(int socket, bool is_blocking) noexcept;

// so_buf: SO_RCVBUF or SO_SNDBUF
void socket_set_buf_size(int socket, bool rcv_buf, int buf_size) noexcept;
int socket_get_buf_size(int socket, bool rcv_buf) noexcept;

int socket_sendto(int socket, const void* data, int size, 
                  uint32_t sin_addr, unsigned short port) noexcept;

int socket_recvfrom(int socket, void* buffer, int maxsize, 
                    unsigned long* sin_addr, unsigned short* port) noexcept;

// @return true if data is available
bool socket_poll_recv(int socket, int timeout_ms) noexcept;
