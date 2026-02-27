#pragma once
#include <rpp/sockets.h>
#include <rpp/debugging.h>

// prints socket error and exits
#define LogErrorExit(format, ...) do { \
    std::string _socket_err = rpp::socket::last_os_socket_err(); \
    _LogError(__log_format(format ": %s", __FILE__, __LINE__, __FUNCTION__) _rpp_wrap_args(__VA_ARGS__), _socket_err.c_str()); \
    exit(1); \
} while(0)
