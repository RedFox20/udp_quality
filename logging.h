#pragma once
#include <rpp/sockets.h>
#include <rpp/debugging.h>

#define RED(text)     "\x1b[91m" text "\x1b[0m"
#define GREEN(text)   "\x1b[92m" text "\x1b[0m"
#define ORANGE(text)  "\x1b[93m" text "\x1b[0m"
#define BLUE(text)    "\x1b[94m" text "\x1b[0m"
#define MAGENTA(text) "\x1b[95m" text "\x1b[0m"
#define CYAN(text)    "\x1b[96m" text "\x1b[0m"
#define WHITE(text)   "\x1b[97m" text "\x1b[0m"

// prints socket error and exits
#define LogErrorExit(format, ...) do { \
    std::string _socket_err = rpp::socket::last_os_socket_err(); \
    _LogError(__log_format(format ": %s", __FILE__, __LINE__, __FUNCTION__) _rpp_wrap_args(__VA_ARGS__), _socket_err.c_str()); \
    exit(1); \
} while(0)
