#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h> // errno
#include <stdarg.h> // va_start
#include <math.h> // round
#if __linux__
    #include <sys/socket.h>
#endif
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
#endif
#include <rpp/sockets.h>
#include <rpp/timer.h>
#include <rpp/strview.h>
#include <rpp/debugging.h>

#define RED(text)     "\x1b[91m" text "\x1b[0m"
#define GREEN(text)   "\x1b[92m" text "\x1b[0m"
#define ORANGE(text)  "\x1b[93m" text "\x1b[0m"
#define BLUE(text)    "\x1b[94m" text "\x1b[0m"
#define MAGENTA(text) "\x1b[95m" text "\x1b[0m"
#define CYAN(text)    "\x1b[96m" text "\x1b[0m"
#define WHITE(text)   "\x1b[97m" text "\x1b[0m"

#define MODE_SLAVE  0
#define MODE_MASTER 1

static void vperrorf(const char* fmt, va_list ap)
{
    std::string err = rpp::socket::last_os_socket_err();
    LogFormatv(LogSeverityError, fmt, ap);
}

static void perrorf(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vperrorf(fmt, ap);
}

static void errorf(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vperrorf(fmt, ap);
    exit(1);
}

// prints socket error and exits
#define LogErrorExit(format, ...) do { \
    std::string _socket_err = rpp::socket::last_os_socket_err(); \
    _LogError(__log_format(format ": %s", __FILE__, __LINE__, __FUNCTION__) _rpp_wrap_args(__VA_ARGS__), _socket_err.c_str()); \
    exit(1); \
} while(0)

static uint32_t parseBytes(rpp::strview literal) noexcept
{
    double value = literal.next_double();
    // and parse the remaining unit string
    if (literal.equalsi("kb"))  return uint32_t(round(value * 1000));
    if (literal.equalsi("kib")) return uint32_t(round(value * 1024));
    if (literal.equalsi("mb"))  return uint32_t(round(value * 1000 * 1000));
    if (literal.equalsi("mib")) return uint32_t(round(value * 1024 * 1024));
    return uint32_t(ceil(value));
}

static std::string toLiteral(uint32_t bytes) noexcept
{
    char buffer[128];
    if      (bytes < 1000)    sprintf(buffer, "%dB", bytes);
    else if (bytes < 1000000) sprintf(buffer, "%.2fKB", double(bytes) / 1000.0);
    else                      sprintf(buffer, "%.2fMB", double(bytes) / 1000000.0);
    return buffer;
}

struct PacketRange
{
    int32_t len;
    int32_t ids[1000];

    void reset() noexcept { len = 0; }
    void push(int32_t id) noexcept
    {
        if (len >= 1000)
        {
            fprintf(stderr, RED("PacketRange full\n"));
            fflush(stderr);
            exit(1);
        }
        ids[len++] = id;
    }
    void printErrors() noexcept
    {
        int32_t lastKnownId = ids[0];
        for (int32_t i = 1; i < len; ++i)
        {
            int32_t expectedId = lastKnownId + 1;
            int32_t actualId = ids[i]; // on error, actualId > expectedId
            if (actualId != expectedId)
            {
                int32_t numMissing = (actualId - expectedId);
                if (numMissing == 1)
                    printf(ORANGE("WARNING: Missing packets 1 seqid %d\n"), expectedId);
                else
                    printf(ORANGE("WARNING: Missing packets %d seqid %d .. %d\n"), numMissing, expectedId, actualId - 1);
            }
            lastKnownId = actualId;
        }
    }
};

