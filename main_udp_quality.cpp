// UDP Quality Analysis Tool
// The server will listen for incoming UDP packets and simply echoes them back to the client.
// The server will also send back Status on how many packets it has received and sent
// The client will simply collect back the Status packets from the server
#include "logging.h"
#include "utils.h"
#include "packets.h"
#include "packet_range.h"
#include "udp_connection.h"
#include <vector>
#include <unordered_map>

#include <rpp/timer.h>

struct Args
{
    int32_t rcvBufSize = 0;
    int32_t sndBufSize = 0;
    int32_t bytesPerBurst = parseSizeLiteral("1MB");
    int32_t bytesPerSec = 0;
    int32_t count = 5;
    int32_t talkback = 0; // talkback packets to send back to server
    int32_t mtu = 1450;
    rpp::ipaddress4 listenerAddr;
    rpp::ipaddress serverAddr;
    rpp::ipaddress bridgeForwardAddr;
    bool blocking = true;
    bool echo = false;
    bool udpc = false;
    bool is_server = false;
    bool is_client = false;
    bool is_bridge = false;
};

void printHelp(int exitCode) noexcept
{
    printf("UDP Quality Tool v1.0 - (c) 2023 KrattWorks\n");
    printf("Usage Client: ./udp_quality --client <ip:port> --size <burst_size> --rate <bytes_per_sec> --buf <socket_buf_size>\n");
    printf("Usage Server: ./udp_quality --listen <listen_port> --buf <socket_buf_size>\n");
    printf("Usage Bridge: ./udp_quality --bridge <listen_port> <to_ip> --buf <socket_buf_size>\n");
    printf("Details:\n");
    printf("    Client controls the main parameters of the test: --rate and --size\n");
    printf("    Server and Bridge only control their own socket buffer size: --buf\n");
    printf("    If Server and Bridge set their own --rate then it will override client\n");
    printf("Options:\n");
    printf("    --listen <listen_port>   Server listens on this port\n");
    printf("    --client <ip:port>       Client connects to this server\n");
    printf("    --bridge <listen_port> <to_ip> Bridge listens on port and forwards to_ip\n");
    printf("    --rate <bytes_per_sec>   Client/Server rate limits, use 0 to disable [default unlimited]\n");
    printf("    --size <bytes>           Client sends this many bytes per burst [default 1MB]\n");
    printf("    --count <iterations>     Client/Server runs this many iterations [default 5]\n");
    printf("    --talkback <bytes>       Server sends this many bytes on its own [default 0]\n");
    printf("    --echo                   Server will also echo all recvd data packets [default false]\n");
    printf("    --mtu <bytes>            Client Only: sets the MTU for the test [default 1450]\n");
    printf("    --buf <buf_size>         Socket SND/RCV buffer size [default: OS configured]\n");
    printf("    --sndbuf <snd_buf_size>  Socket SND buffer size [default: OS configured]\n");
    printf("    --rcvbuf <rcv_buf_size>  Socket RCV buffer size [default: OS configured]\n");
    printf("    --blocking               Uses blocking sockets [default]\n");
    printf("    --nonblocking            Uses nonblocking sockets\n");
    printf("    --udpc                   Uses alternative UDP C socket implementation\n");
    printf("    --help\n");
    printf("  When running from ubuntu, sudo is required\n");
    printf("  All rates can be expressed as a number followed by a unit:\n");
    printf("        1000 = 1000 bytes   1KB  = 1000 bytes   1MB  = 1000*1000 bytes\n");
    printf("                            1KiB = 1024 bytes   1MiB = 1024*1024 bytes \n");
    exit(1);
}

static const uint8_t DATA[] = {
    0xCF, 0x26, 0xBD, 0xE0, 0x39, 0x7E, 0xCA, 0xD5, 0xEF, 0xA8, 0x26, 0x3C, 0x5F, 0x04, 0x18, 0x8D, 0x07, 0xB0, 0x93, 0x7D, 0xED, 0xA3, 0x46, 0x89, 0x4E, 0x0F, 0xA1, 0xC2, 0x29, 0x36, 0x15, 0x91, 
    0xB7, 0x35, 0x09, 0x89, 0x7F, 0x96, 0xE9, 0x2D, 0x30, 0x70, 0x48, 0xD5, 0x8A, 0x84, 0x7D, 0x70, 0x8B, 0xB7, 0x2D, 0xCA, 0xB6, 0x7A, 0xF5, 0xE0, 0x23, 0x9A, 0x47, 0x01, 0x47, 0x50, 0x1B, 0xB4, 
    0xE2, 0xE1, 0x49, 0x1D, 0x67, 0xAB, 0x70, 0xE0, 0x86, 0x86, 0x36, 0xF5, 0x10, 0xA5, 0x64, 0x73, 0xA9, 0xB7, 0xE6, 0x15, 0x61, 0x5B, 0xE4, 0xCD, 0xA4, 0xE2, 0xE5, 0x5D, 0x6E, 0x68, 0x49, 0xBE, 
    0x64, 0x02, 0x70, 0x06, 0x17, 0x98, 0x74, 0x68, 0x33, 0x66, 0x51, 0x36, 0x49, 0x0B, 0x49, 0x2C, 0xED, 0x5B, 0x01, 0xC0, 0x72, 0xE0, 0x96, 0x73, 0x35, 0xE4, 0x6D, 0x0E, 0xB8, 0xBA, 0xAC, 0xD6, 
    0x50, 0x84, 0xE9, 0x48, 0x7E, 0x22, 0x4C, 0x3B, 0x39, 0x3C, 0x96, 0xD4, 0xBE, 0xF6, 0x06, 0x55, 0xA2, 0x3F, 0x34, 0x9B, 0x97, 0x94, 0xBE, 0x32, 0xBE, 0x54, 0x69, 0x16, 0xA0, 0x75, 0xE4, 0x37, 
    0xE4, 0x4E, 0xBC, 0x38, 0x89, 0xAE, 0xBF, 0x5F, 0x1F, 0x12, 0xA1, 0x1F, 0xA9, 0x5F, 0x8B, 0x52, 0xC9, 0x94, 0x2F, 0xBC, 0x02, 0xAE, 0x7A, 0xA7, 0x98, 0x34, 0x44, 0xD1, 0x9E, 0x58, 0xD1, 0x32,
    0xD3, 0x4A, 0xE9, 0x13, 0x10, 0xCB, 0xDE, 0xF4, 0x00, 0x1B, 0xDB, 0x35, 0x12, 0xEC, 0x70, 0xF2, 0x2E, 0xA6, 0xE8, 0xCE, 0xDB, 0x4B, 0x04, 0xAC, 0xD4, 0xE6, 0xE1, 0x46, 0x0D, 0x9F, 0x63, 0xAB,
    0xC3, 0x9C, 0x74, 0x80, 0x19, 0x5D, 0xCD, 0xF3, 0x8D, 0xCC, 0x7C, 0x2C, 0x28, 0x4C, 0xCD, 0xBA, 0xC3, 0x19, 0xA3, 0x59, 0x47, 0x6B, 0x54, 0x0C, 0x5F, 0x26, 0x5A, 0x19, 0x41, 0xFA, 0x77, 0x5F,
    0xD0, 0x85, 0x48, 0x92, 0x68, 0x23, 0x53, 0xAF, 0x79, 0x79, 0x91, 0x88, 0xF4, 0x71, 0xB0, 0xBA, 0xB8, 0x6C, 0x2A, 0x8C, 0x2E, 0xB4, 0x6F, 0x24, 0x83, 0x65, 0x4B, 0x58, 0x56, 0x65, 0x9E, 0x7B,
    0xB2, 0x1E, 0xE8, 0x9E, 0xA2, 0x57, 0x1F, 0xF3, 0x4B, 0x25, 0x98, 0xDD, 0xD5, 0xB2, 0x6E, 0x6E, 0xBE, 0xBF, 0xF2, 0xEA, 0x67, 0xBA, 0x25, 0x05, 0x84, 0x30, 0x9E, 0x9A, 0xC5, 0x66, 0x0B, 0x21,
    0x43, 0xEB, 0x1E, 0x50, 0xC6, 0xA8, 0x8C, 0xAB, 0x65, 0x76, 0x54, 0x76, 0xB6, 0xF7, 0x4C, 0x0F, 0xCC, 0x83, 0xAA, 0x93, 0xF1, 0x3E, 0x82, 0x37, 0xED, 0x9D, 0xFD, 0x19, 0xB9, 0x34, 0x2E, 0x93,
    0x67, 0x6A, 0x6E, 0x90, 0x68, 0xE6, 0x2F, 0x57, 0x1C, 0x5A, 0x30, 0xF4, 0xCB, 0xC2, 0x58, 0x51, 0x28, 0xD3, 0x8F, 0xF7, 0x53, 0x90, 0x4B, 0xED, 0x4D, 0x9C, 0x9B, 0x6D, 0x8D, 0x6E, 0x6B, 0x3E,
    0x65, 0xD0, 0x9A, 0xC2, 0x99, 0x9F, 0x6C, 0x1E, 0xA7, 0xE4, 0xA8, 0x91, 0xAB, 0xC0, 0xEE, 0x52, 0x86, 0x32, 0xAC, 0x4B, 0x33, 0x79, 0x56, 0x0C, 0x9E, 0x03, 0xDB, 0x8C, 0xD5, 0x00, 0xE4, 0xBC,
    0xD2, 0x9A, 0x7D, 0xF7, 0x8D, 0x98, 0xD5, 0xDE, 0xB5, 0xDE, 0xC6, 0x94, 0xEB, 0xBB, 0x9E, 0x7E, 0xC9, 0xE2, 0xB5, 0x3E, 0x11, 0x7A, 0x5A, 0xDC, 0xE9, 0x63, 0x9D, 0x09, 0x29, 0x4F, 0xF5, 0x92,
    0xFC, 0x8C, 0x35, 0x9B, 0x3C, 0xC2, 0x35, 0x62, 0xE5, 0x08, 0x3B, 0x68, 0x08, 0x95, 0x45, 0xD5, 0x23, 0x4E, 0xD0, 0x8F, 0x2E, 0xBF, 0xEF, 0x80, 0xB4, 0x96, 0xBC, 0xF5, 0xA0, 0x06, 0xCA, 0xCA,
    0x57, 0x07, 0xA2, 0x09, 0x7D, 0x22, 0xF1, 0xE8, 0x02, 0x18, 0xA7, 0x4A, 0x51, 0x50, 0xD5, 0xF0, 0x2E, 0xAC, 0x4D, 0x84, 0xB2, 0x1D, 0xD9, 0x63, 0x9F, 0x61, 0xA1, 0x01, 0xE8, 0x5A, 0xBD, 0x32,
    0x83, 0x8B, 0x46, 0xE1, 0x8B, 0x07, 0xC6, 0xF3, 0x1F, 0xFC, 0xC0, 0x32, 0x4D, 0x64, 0xEC, 0x6E, 0xA2, 0x46, 0x03, 0x1A, 0xC9, 0x44, 0x00, 0xE2, 0x89, 0x50, 0x64, 0x93, 0x6A, 0xC0, 0x98, 0xDE,
    0x41, 0x92, 0x4D, 0x1A, 0xF5, 0x5C, 0x9D, 0xF3, 0x16, 0xE2, 0x78, 0xD2, 0x56, 0xBE, 0xA5, 0x9B, 0x51, 0xBF, 0x8C, 0xDD, 0x9B, 0xCC, 0x5B, 0xF3, 0x09, 0xFC, 0x61, 0xDE, 0xC6, 0xBE, 0xE3, 0x2C,
    0xDB, 0x97, 0x8A, 0x46, 0x98, 0xB3, 0x1D, 0xE0, 0x2B, 0xB1, 0x3C, 0x65, 0x2D, 0x5B, 0x6F, 0x9A, 0xE4, 0xF5, 0x55, 0x21, 0xA3, 0x5C, 0xEC, 0x66, 0x71, 0x61, 0x7D, 0xA4, 0xDE, 0x4C, 0x5D, 0xEC,
    0xFB, 0x4E, 0x21, 0x7E, 0xF9, 0xC5, 0xB6, 0xD2, 0x4D, 0x61, 0xD2, 0xB2, 0xC3, 0xA5, 0x6D, 0x82, 0x3B, 0x8A, 0xBD, 0x15, 0x41, 0x2F, 0xA5, 0x5B, 0x5B, 0x41, 0x0A, 0x45, 0x9B, 0x9E, 0x85, 0x98,
    0xCE, 0x9C, 0xC1, 0xCF, 0xDB, 0x22, 0xAC, 0x5A, 0xA5, 0x6E, 0xAA, 0x40, 0xB8, 0x42, 0x4A, 0x93, 0x49, 0x5F, 0x39, 0x56, 0x5C, 0xA0, 0xF6, 0xE9, 0xE2, 0xC0, 0x6F, 0x3A, 0x1D, 0x49, 0xDF, 0xDC,
    0xC9, 0xBC, 0x46, 0x9C, 0xD3, 0x3C, 0x18, 0x69, 0xAE, 0x2B, 0x88, 0x2B, 0x80, 0xC5, 0x4A, 0x26, 0x2A, 0xC1, 0x73, 0x8C, 0xFD, 0x0C, 0x47, 0x25, 0xB0, 0xF9, 0x9D, 0x9A, 0x02, 0x49, 0x04, 0xE3,
    0x1A, 0x50, 0x77, 0x5C, 0x15, 0xC2, 0x91, 0x05, 0x87, 0x60, 0xAB, 0x3D, 0x59, 0xB5, 0x30, 0x6C, 0xA0, 0xB9, 0xA5, 0xDA, 0x9D, 0xA0, 0xDF, 0xE8, 0xCD, 0x8E, 0xA8, 0x68, 0x12, 0x80, 0x3E, 0x32,
    0x01, 0xDE, 0x27, 0x68, 0xEC, 0xCC, 0x54, 0xDE, 0x96, 0x97, 0xA0, 0x8B, 0xEA, 0x66, 0xD2, 0xB2, 0x01, 0x6A, 0x2E, 0x51, 0x26, 0xCB, 0x1D, 0x53, 0x3F, 0xA4, 0xF6, 0x53, 0x22, 0xA3, 0x9C, 0xC8,
    0xB8, 0x8A, 0x50, 0xCB, 0x6C, 0xCF, 0xBB, 0x34, 0x44, 0xE0, 0x7C, 0x54, 0x3A, 0x34, 0x35, 0xB9, 0xE4, 0xBD, 0xD3, 0x26, 0xE3, 0x69, 0x49, 0x51, 0xA2, 0xE9, 0x75, 0xC9, 0xF6, 0xDF, 0x57, 0x9E,
    0x76, 0xEC, 0x2C, 0xBB, 0x17, 0xCA, 0xCA, 0x28, 0x84, 0x9B, 0x44, 0xFE, 0x46, 0x0A, 0x43, 0xBF, 0xBC, 0x4E, 0xBC, 0xBC, 0x0A, 0xC7, 0x6E, 0x39, 0xAA, 0x77, 0x4F, 0x27, 0xCB, 0xA8, 0xF9, 0xF4,
    0xDE, 0x0E, 0x3F, 0x5F, 0x55, 0x2F, 0x35, 0x37, 0xC7, 0x03, 0xF7, 0xDA, 0xE9, 0xE2, 0xEE, 0x0E, 0xA0, 0xDA, 0xF8, 0x58, 0x14, 0x60, 0x5F, 0xEF, 0x99, 0x28, 0x84, 0x4C, 0x43, 0x83, 0x79, 0x78,
    0x79, 0x0F, 0x1F, 0x42, 0x62, 0xE8, 0xA4, 0x22, 0x5E, 0x43, 0x72, 0x6B, 0x51, 0xDB, 0x6D, 0x32, 0xEF, 0xB8, 0xDB, 0xFB, 0x09, 0x83, 0xCF, 0x4A, 0x9D, 0x34, 0x42, 0xB8, 0x5D, 0xB4, 0x11, 0xC1,
    0x79, 0xD0, 0x89, 0x26, 0x5E, 0x98, 0x99, 0x44, 0xF8, 0xF6, 0x1C, 0xAF, 0xAF, 0xCB, 0xB1, 0xF9, 0x11, 0x12, 0x50, 0x17, 0xAC, 0x78, 0x4E, 0x22, 0xB9, 0xAD, 0xC7, 0x0A, 0x04, 0xDD, 0x7B, 0xE9,
    0x60, 0xB4, 0x87, 0x1A, 0xC1, 0xD2, 0x42, 0xC6, 0xEB, 0x1A, 0xA4, 0xB4, 0xCD, 0x73, 0x70, 0x41, 0xB3, 0x35, 0xD8, 0x97, 0xAC, 0xBE, 0x44, 0x4C, 0xB3, 0x37, 0xB1, 0xE7, 0x77, 0x74, 0xCA, 0x83,
    0xAD, 0xC4, 0x9F, 0x29, 0xD1, 0x70, 0xE2, 0x8B, 0x95, 0xBD, 0x51, 0x5D, 0xB1, 0x8E, 0x18, 0x3E, 0x76, 0xE6, 0x73, 0x5E, 0x97, 0xC9, 0x98, 0x13, 0x95, 0x6F, 0xF5, 0xB0, 0x6B, 0xFA, 0x30, 0x86,
    0x41, 0x35, 0x8D, 0xB7, 0x1D, 0xB7, 0x4B, 0xBF, 0x91, 0xCF, 0x02, 0xAC, 0x86, 0x11, 0x55, 0xC8, 0x47, 0xEE, 0x8F, 0x61, 0x4B, 0xF1, 0x92, 0xD4, 0x7D, 0x1B, 0xFF, 0x16, 0xE5, 0xF2, 0x65, 0xED,
    0xD8, 0xBA, 0x57, 0x46, 0xB0, 0x69, 0x39, 0xF2, 0x0B, 0xB6, 0x7F, 0xF9, 0x60, 0x7E, 0x45, 0x34, 0x7C, 0xEC, 0x98, 0x7C, 0xBE, 0x5F, 0x19, 0xC1, 0x8F, 0xA5, 0x5A, 0x48, 0x2A, 0x74, 0xC2, 0x74,
    0xAB, 0xC6, 0x3B, 0x07, 0xC1, 0x9B, 0x71, 0x2B, 0x84, 0x00, 0xA1, 0x1D, 0xE9, 0x80, 0x75, 0x66, 0x01, 0x6E, 0x80, 0xAC, 0x9E, 0x72, 0xB3, 0x57, 0x0D, 0xB9, 0xA0, 0xC8, 0xF6, 0x9E, 0x63, 0x33,
    0x3C, 0xDF, 0xE7, 0x9A, 0x3E, 0x02, 0x0B, 0xC2, 0xF8, 0x14, 0xCF, 0x0E, 0x19, 0x4C, 0x3D, 0x1E, 0x4F, 0x6F, 0xA2, 0x24, 0xDF, 0xF8, 0xD6, 0xC8, 0x27, 0x1B, 0x7F, 0x52, 0x3D, 0x98, 0x88, 0x31,
    0x66, 0x54, 0x70, 0xB9, 0x91, 0xB4, 0x6D, 0x8F, 0xC7, 0xD3, 0x45, 0xF4, 0xC6, 0xE9, 0xA2, 0x4D, 0x67, 0x1B, 0x64, 0x05, 0x48, 0x12, 0xB4, 0x29, 0x47, 0x8E, 0x62, 0xA1, 0xCA, 0xC6, 0xC1, 0x1F,
    0x29, 0x69, 0x16, 0xCF, 0x7C, 0x1B, 0x61, 0xDC, 0xA4, 0xA3, 0x0B, 0x2A, 0x39, 0xCE, 0x88, 0x0B, 0x2E, 0x17, 0x00, 0xF6, 0xCC, 0xAE, 0x62, 0x83, 0x25, 0x63, 0x11, 0xEB, 0xC6, 0x38, 0xC3, 0x6D,
    0xD8, 0x6B, 0x7F, 0x3F, 0x71, 0xB0, 0x25, 0x89, 0x9F, 0x4D, 0xD3, 0x3D, 0x7B, 0xC3, 0xD7, 0x19, 0x18, 0x82, 0x70, 0x7C, 0x6F, 0x54, 0xA7, 0x70, 0xE4, 0x14, 0x41, 0x9C, 0xD3, 0x11, 0x08, 0xC9,
    0x7D, 0x39, 0x33, 0xF5, 0xF8, 0xB5, 0x8E, 0xB1, 0x07, 0xA4, 0x7B, 0x28, 0x06, 0xB1, 0x1C, 0x53, 0x44, 0xE7, 0x3A, 0x00, 0x8D, 0xE6, 0xBB, 0x05, 0x1B, 0xF3, 0x35, 0xC4, 0x8A, 0x1F, 0x2F, 0x55,
    0x58, 0x7E, 0x3B, 0x7F, 0xE2, 0x66, 0x8B, 0x0E, 0xF7, 0x72, 0xFF, 0xB1, 0xA6, 0x8F, 0x81, 0xDA, 0xB9, 0xD2, 0x64, 0x07, 0xFB, 0x42, 0x9F, 0x3C, 0xDB, 0xC2, 0x37, 0x10, 0xA8, 0x48, 0x3D, 0x4B,
    0x13, 0x65, 0x38, 0xA5, 0xDE, 0x74, 0x10, 0xCE, 0xBF, 0x3E, 0x18, 0xE1, 0xB7, 0xF9, 0xAD, 0x83, 0xFD, 0x64, 0x59, 0x1A, 0xEA, 0xF5, 0x4C, 0x90, 0xC5, 0x41, 0x6B, 0x06, 0x76, 0xB9, 0xDF, 0x05,
    0x38, 0x83, 0xD4, 0xBC, 0xF0, 0xEE, 0x93, 0x7A, 0xC7, 0xFE, 0x12, 0x04, 0x1D, 0x40, 0xD5, 0xA9, 0x0B, 0xC0, 0x57, 0x77, 0x23, 0x6C, 0xC5, 0xA4, 0x12, 0x97, 0x29, 0x28, 0x85, 0x37, 0x72, 0x4E,
    0x6E, 0xFD, 0xF3, 0xEF, 0x38, 0xC4, 0xA9, 0x5A, 0xF4, 0xB4, 0x5E, 0xA8, 0xEC, 0x7D, 0x6F, 0x51, 0x0A, 0xDA, 0xAA, 0x16, 0xD1, 0x00, 0xD7, 0x5F, 0xB9, 0x1B, 0x06, 0xD5, 0x11, 0x3D, 0x62, 0xDB,
    0x38, 0x19, 0x7D, 0x58, 0xBA, 0xC8, 0x69, 0x5E, 0x78, 0x83, 0xDD, 0xC5, 0x8A, 0xCE, 0xCA, 0xA7, 0x4C, 0xB2, 0xA1, 0x29, 0x32, 0x1F, 0x4B, 0x62, 0xC4, 0xDB, 0xB7, 0x6D, 0xB7, 0x2F, 0xEA, 0xBB,
    0xA8, 0x8F, 0xB1, 0xCF, 0x81, 0x6A, 0xE9, 0x78, 0x46, 0x98, 0x67, 0x96, 0x99, 0x80, 0xE4, 0x7D, 0xE8, 0x8C, 0x13, 0xE6, 0xD6, 0x94, 0x44, 0x5F, 0x4D, 0x9F, 0x4E, 0xD6, 0x9C, 0x2A, 0x12, 0x23,
    0xBB, 0x32, 0x31, 0xD3, 0x28, 0x54, 0x98, 0x03, 0xCD, 0x3F, 0xCD, 0x4E, 0x9B, 0x5F, 0x0C, 0x8D, 0x85, 0xD9, 0x03, 0x69, 0x16, 0x74, 0x6E, 0x8D, 0x57, 0x8A, 0xFC, 0x56, 0xE1, 0x1E, 0x78, 0x52,
    0x9E, 0xAE, 0x3F, 0x4D, 0xB7, 0xCF, 0xA9, 0x37, 0x0C, 0x10, 0x03, 0x79, 0xF5, 0xB1, 0x51, 0x4A, 0x83, 0x79, 0x0C, 0xFD, 0x36, 0xB1, 0x23, 0x20, 0x78, 0x26, 0x19, 0xDA,
};
static const int DATA_SIZE = sizeof(DATA);

// write the pseudo-random deterministic data sequence
static void writeDataSequence(char* buffer, int size) noexcept
{
    int srcIdx = 0;
    for (int i = 0; i < size; ++i) {
        buffer[i] = (char)DATA[srcIdx];
        if (++srcIdx >= DATA_SIZE) srcIdx = 0;
    }
}

static bool checkDataSequence(const char* buffer, int size) noexcept
{
    int srcIdx = 0;
    for (int i = 0; i < size; ++i) {
        if (buffer[i] != DATA[srcIdx]) return false;
        if (++srcIdx >= DATA_SIZE) srcIdx = 0;
    }
    return true;
}

struct UDPQuality
{
    Args args;
    UDPConnection c;
    EndpointType whoami = EndpointType::SERVER; // who am I?
    EndpointType talkingTo = EndpointType::CLIENT; // who am I talking to?

    int32_t statusSeqId = 0; // seqId for our status messages
    int32_t statusIteration = 0; // which iteration of the test this is
    int32_t burstCount = 0; // how many packets CLIENT sends in a burst
    int32_t talkbackCount = 0; // how many packets SERVER talkbacks in a burst?

    explicit UDPQuality(const Args& _args) noexcept
        : args{_args}, c{!_args.udpc} {}

    struct PacketInfo { int32_t count = 0; };

    // all kinds of traffic statistics and state to find traffic bugs
    struct TrafficStatus
    {
        EndpointType sender = EndpointType::UNKNOWN;
        int32_t sent = 0; // data packets sent TO SENDER
        int32_t received = 0; // data packets recvd FROM SENDER

        int32_t lastReceivedSeqId = 0; // last received seqid from SENDER
        int32_t outOfOrderPackets = 0; // SENDER sent X packets out of order
        int32_t duplicatePackets = 0; // SENDER sent X duplicate packets
        int32_t loopedPackets = 0; // SENDER saw its own data packets
        int32_t invalidData = 0; // RECEIVER saw invalid data in the packet, so it was corrupted

        std::unordered_map<int32_t, PacketInfo> packets;
        PacketRange receivedRange;
        Packet lastStatus;
    };

    TrafficStatus clientCh; // traffic FROM / TO client
    TrafficStatus serverCh; // traffic FROM / TO server
    TrafficStatus unknownCh; // traffic FROM / TO unknown

    void reset(const Packet& clientInit) noexcept {
        args.echo = clientInit.echo != 0;
        args.mtu = clientInit.mtu;
        burstCount = clientInit.burstCount;
        talkbackCount = clientInit.talkbackCount;
        statusSeqId = 0;
        statusIteration = clientInit.iteration;
        int32_t rateLimit = args.bytesPerSec > 0
                          ? args.bytesPerSec : clientInit.maxBytesPerSecond;
        c.balancer.set_max_bytes_per_sec(rateLimit);
        clientCh = { EndpointType::CLIENT };
        serverCh = { EndpointType::SERVER };
        unknownCh = { EndpointType::UNKNOWN };
    }

    TrafficStatus& traffic(EndpointType which) noexcept {
        if (which == EndpointType::SERVER) return serverCh;
        if (which == EndpointType::CLIENT) return clientCh;
        return unknownCh;
    }

    void sendDataPacket(EndpointType toWhom, const rpp::ipaddress& toAddr) noexcept {
        auto buf = std::vector<uint8_t>(args.mtu, '\0');
        Data* data = reinterpret_cast<Data*>(buf.data());
        data->type = PacketType::DATA;
        data->status = StatusType::BURST_START;
        data->sender = whoami;
        data->echo = args.echo;
        data->seqid = traffic(toWhom).sent;
        data->len = args.mtu; // pkt len

        int bufSize = data->size(args.mtu);
        writeDataSequence(data->buffer, bufSize);

        if (c.sendPacketTo(*data, args.mtu, toAddr))
            traffic(toWhom).sent++;
    }

    bool sendStatusPacket(StatusType status, const rpp::ipaddress& to) noexcept {
        Packet st;
        st.type = PacketType::STATUS;
        st.status = status;
        st.sender = whoami;

        st.echo = args.echo;
        st.seqid = statusSeqId++;
        st.len = sizeof(Packet);
        st.iteration = statusIteration;
        st.burstCount = burstCount;
        st.talkbackCount = talkbackCount;

        st.dataSent = traffic(talkingTo).sent;
        st.dataReceived = traffic(talkingTo).received;
        st.maxBytesPerSecond = c.balancer.get_max_bytes_per_sec();
        st.mtu = args.mtu;
        printStatus("send", st);
        return c.sendPacketTo(st, sizeof(st), to);
    }

    void printStatus(const char* recvOrSend, const Packet& p) const noexcept {
        LogInfo("   %s from %s STATUS it=%d %12s:   sent:%d recv:%d", recvOrSend,
                to_string(p.sender), p.iteration, to_string(p.status), p.dataSent, p.dataReceived);
    }

    Packet* recvStatusFrom(rpp::ipaddress& from, int timeoutMillis) noexcept {
        int received = c.recvPacketFrom(from, timeoutMillis);
        if (received > 0) {
            Packet* p = &c.getReceivedPacket();
            if (p->type != PacketType::STATUS) {
                LogError(RED("recv STATUS invalid packet.type:%d from: %s"), int(p->type), rpp::socket::last_os_socket_err());
                return nullptr;
            }
            onStatusReceived(*p);
            return p;
        }
        if (received == 0) LogError(RED("recv STATUS timeout"));
        return nullptr;
    }

    void onDataReceived(Data& p) noexcept {
        TrafficStatus& tr = traffic(p.sender);
        tr.received++;

        if (p.seqid < tr.lastReceivedSeqId) {
            tr.outOfOrderPackets++;
        } else if (p.seqid > tr.lastReceivedSeqId) {
            tr.receivedRange.push(p.seqid);
        }
        tr.lastReceivedSeqId = p.seqid;

        PacketInfo& pktInfo = tr.packets[p.seqid];
        ++pktInfo.count;
        if (pktInfo.count > 1) {
            tr.duplicatePackets++;
        }
        if (!checkDataSequence(p.buffer, p.len)) {
            tr.invalidData++;
        }
    }

    void onStatusReceived(Packet& p) noexcept {
        printStatus("recv", p);
        TrafficStatus& tr = traffic(p.sender);
        tr.lastStatus = p;
    }

    void client() noexcept
    {
        whoami = EndpointType::CLIENT;
        talkingTo = EndpointType::SERVER;
        burstCount = args.bytesPerBurst / args.mtu;
        if (args.talkback > 0) {
            talkbackCount = args.talkback / args.mtu;
        }

        rpp::ipaddress toServer = args.serverAddr;
        rpp::ipaddress actualServer;

        if (!sendStatusPacket(StatusType::INIT, toServer))
            LogErrorExit(RED("Failed to send INIT packet"));

        // and wait for response
        if (Packet* st = recvStatusFrom(actualServer, /*timeoutMillis*/2000)) {
            if (st->status != StatusType::INIT) LogErrorExit(RED("Handshake failed"));
            LogInfo(GREEN("Received HANDSHAKE: %s"), actualServer.str());
        } else LogErrorExit(RED("Handshake failed"));

        // with count=5, statusIteration will be 1,2,3,4,5
        for (statusIteration = 1; statusIteration <= args.count; )
        {
            int32_t totalSize = args.mtu * burstCount;
            LogInfo(MAGENTA(">> SEND BURST pkts:%d  size:%s  rate:%s"), 
                    burstCount, toLiteral(totalSize), toRateLiteral(args.bytesPerSec));
            sendStatusPacket(StatusType::BURST_START, actualServer);
            traffic(talkingTo).receivedRange.reset();

            int32_t gotTalkback = 0;
            bool gotBurstFinish = false;

            auto handleRecv = [&](Packet& p) {
                if (p.type == PacketType::DATA) {
                    ++gotTalkback;
                    onDataReceived(reinterpret_cast<Data&>(p));
                } else if (p.type == PacketType::STATUS) {
                    onStatusReceived(p);
                    if (p.status == StatusType::BURST_FINISH && p.iteration == statusIteration) {
                        gotBurstFinish = true;
                        LogInfo(MAGENTA(">> SEND BURST FINISHED recvd:%dpkts"), gotTalkback);
                        printSummary(statusIteration);
                        LogInfo("\x1b[0m|---------------------------------------------------------|");
                    }
                }
            };
            auto waitAndRecvForDuration = [&](int32_t durationMs) {
                rpp::Timer timer { rpp::Timer::AutoStart };
                while (!gotBurstFinish && timer.elapsed_ms() < durationMs) {
                    if (Packet* p = c.tryRecvPacket(/*timeoutMillis*/15)) {
                        handleRecv(*p);
                    }
                }
            };

            rpp::Timer dataStart { rpp::Timer::AutoStart };
            for (int32_t j = 0; j < burstCount; ++j) {
                sendDataPacket(talkingTo, actualServer);
                // since we are rate limited anyway, poll for a few packets
                for (int i = 0; i < 20 && c.pollRead(); ++i) {
                    if (Packet* p = c.tryRecvPacket())
                        handleRecv(*p);
                }
            }
            double dataElapsedMs = dataStart.elapsed_millis();
            int32_t actualBytesPerSec = int32_t((totalSize * 1000.0) / (dataElapsedMs));
            LogInfo(MAGENTA(">> SEND ELAPSED %.2fms  actualrate:%s  recvd:%dpkts"), 
                    dataElapsedMs, toRateLiteral(actualBytesPerSec), gotTalkback);

            // we always wait a bit longer, just incase we are getting any bogus packets
            // we want to be aware that we receive too many packets
            int32_t numTalkback = talkbackCount + (args.echo ? burstCount : 0);
            if (numTalkback > 0) {
                int32_t expectedTalkbackBytes = numTalkback * args.mtu;
                int32_t minTalkbackMs = (expectedTalkbackBytes * 1000) / actualBytesPerSec;
                LogInfo(MAGENTA(">> WAITING TALKBACK %dms expected:%dpkts"), minTalkbackMs, numTalkback);
                waitAndRecvForDuration(minTalkbackMs);
            }

            // wait enough time before sending a burst finish
            rpp::sleep_ms(300);
            LogInfo(MAGENTA(">> SEND BURST FINISH recvd:%dpkts"), gotTalkback);
            // after we've waited enough, send BURST_FINISH
            if (!sendStatusPacket(StatusType::BURST_FINISH, actualServer))
                LogErrorExit(RED("Failed to send STATUS packet"));

            waitAndRecvForDuration(5000);
            if (!gotBurstFinish) {
                LogInfo(RED("timeout waiting BURST_FINISH ACK"));
            }

            if (statusIteration == args.count)
                break; // we're done
            ++statusIteration;
        }

        rpp::sleep_ms(500); // wait a bit, send finish and wait for FINISHED status
        sendStatusPacket(StatusType::FINISHED, actualServer);

        if (toServer != actualServer)
            LogInfo(ORANGE("Client connected to %s but received data from %s"), toServer.str(), actualServer.str());
        printSummary(statusIteration);
    }

    void server() noexcept
    {
        whoami = EndpointType::SERVER;
        talkingTo = EndpointType::CLIENT;
        rpp::ipaddress clientAddr;
        int32_t talkbackRemaining = 0;

        while (true) // receive packets infinitely
        {
            int timeout = talkbackRemaining > 0 ? 0 : 100;
            int rcvlen = c.recvPacketFrom(clientAddr, /*timeoutMillis*/timeout);

            // send talkback packets when possible
            if (talkbackRemaining > 0) {
                sendDataPacket(talkingTo, clientAddr);
                --talkbackRemaining;
            }

            if (rcvlen <= 0)
                continue;

            Packet& p = c.getReceivedPacket();
            if (p.type == PacketType::DATA) {
                onDataReceived(reinterpret_cast<Data&>(p));
                if (args.echo) {
                    p.sender = whoami; // server echoing it now
                    if (c.sendPacketTo(p, rcvlen, clientAddr)) clientCh.sent++;
                    else LogInfo(ORANGE("Failed to echo packet: %d"), p.seqid);
                }
            } else if (p.type == PacketType::STATUS) {
                if (p.status == StatusType::INIT) { // Client is initializing a new session
                    LogInfo("\x1b[0m===========================================================");
                    reset(p); // RESET before updating traffic stats
                    onStatusReceived(p);
                    sendStatusPacket(StatusType::INIT, clientAddr); // echo back the init handshake
                    LogInfo("   STARTED it=%d: %s  rate:%s  rcvbuf:%s  sndbuf:%s", 
                            p.iteration, clientAddr.str(),
                            toRateLiteral(c.getRateLimit()), 
                            toLiteral(c.getBufSize(rpp::socket::BO_Recv)),
                            toLiteral(c.getBufSize(rpp::socket::BO_Send)));
                } else if (p.status == StatusType::BURST_START) {
                    LogInfo("\x1b[0m|---------------------------------------------------------|");
                    onStatusReceived(p);
                    statusIteration = p.iteration;
                    talkbackRemaining = talkbackCount;
                    if (talkbackRemaining > 0) {
                        LogInfo("   SEND TALKBACK pkts:%d  size:%s  rate:%s", 
                            talkbackCount, toLiteral(talkbackCount*args.mtu),
                            toRateLiteral(c.getRateLimit()));
                    }
                    sendStatusPacket(StatusType::BURST_START, clientAddr);
                } else if (p.status == StatusType::BURST_FINISH) {
                    onStatusReceived(p);
                    sendStatusPacket(StatusType::BURST_FINISH, clientAddr);
                    printSummary(statusIteration);
                } else if (p.status == StatusType::FINISHED) {
                    onStatusReceived(p);
                    sendStatusPacket(StatusType::FINISHED, clientAddr); // echo back the finished handshake
                    printSummary(statusIteration);
                    talkbackRemaining = 0;
                    LogInfo("\x1b[0m===========================================================");
                }
            }
        }
    }

    // bridge runs forever and simply forwards any packets to server
    void bridge()
    {
        whoami = EndpointType::BRIDGE;
        talkingTo = EndpointType::UNKNOWN;
        rpp::ipaddress clientAddr;
        rpp::ipaddress serverAddr = args.bridgeForwardAddr;
        while (true)
        {
            rpp::ipaddress from;
            int recvlen = c.recvPacketFrom(from, /*timeoutMillis*/100);
            if (recvlen <= 0)
                continue;

            Packet& p = c.getReceivedPacket();
            if (p.type == PacketType::STATUS) {
                bool print = false;
                if (p.sender == EndpointType::CLIENT) {
                    if (p.status == StatusType::INIT) {
                        clientAddr = from;
                        LogInfo("   BRIDGE init client=%s -> server=%s", clientAddr.str(), serverAddr.str());
                        reset(p);
                    }
                } else if (p.sender == EndpointType::SERVER) {
                    if (p.status == StatusType::BURST_START) {
                        statusIteration = p.iteration;
                    } else if (p.status == StatusType::BURST_FINISH || p.status == StatusType::FINISHED) {
                        print = true;
                    }
                }
                onStatusReceived(p);
                if (print) printSummary(statusIteration);
            } else if (p.type == PacketType::DATA) {
                onDataReceived(reinterpret_cast<Data&>(p));
            }

            EndpointType forwardTo = EndpointType::UNKNOWN;
            if (p.sender == EndpointType::CLIENT) {
                if (from != clientAddr) LogWarning("BRIDGE received packet from unknown client: %s", from.str());
                forwardTo = EndpointType::SERVER;
            } else if (p.sender == EndpointType::SERVER) {
                forwardTo = EndpointType::CLIENT;
            }

            if (forwardTo != EndpointType::UNKNOWN) {
                if (rpp::ipaddress to = forwardTo == EndpointType::CLIENT ? clientAddr : serverAddr) {
                    if (c.sendPacketTo(p, recvlen, to)) {
                        if (p.type == PacketType::DATA)
                            traffic(forwardTo).sent++;
                    } else LogError(ORANGE("Failed to forward packet: %d  %s"), p.seqid, to.str());
                }
            }
        }
    }

    void printSummary(int iteration) noexcept
    {
        if (whoami == EndpointType::CLIENT) {
            // server must have received all the packets that client sent
            printReceivedAt("SERVER", /*expected*/serverCh.sent, /*actual*/serverCh.lastStatus.dataReceived, serverCh.invalidData);

            // we must know how many packets SERVER should send back to us
            int32_t expectedFromServer = (args.echo ? serverCh.sent : 0) + talkbackCount*iteration;
            if (expectedFromServer > 0) {
                printReceivedAt("CLIENT", expectedFromServer, serverCh.received, clientCh.invalidData);
            }
        } else if (whoami == EndpointType::SERVER) {
            // server must have received all the packets that client sent
            printReceivedAt("SERVER", /*expected*/clientCh.lastStatus.dataSent, /*actual*/clientCh.received, clientCh.invalidData);

            // client must have received all the packets that it sent + talkback
            int32_t expectedAtClient = 0;
            if (args.echo) expectedAtClient = clientCh.lastStatus.dataSent;
            if (talkbackCount > 0) expectedAtClient += talkbackCount*iteration;
            if (expectedAtClient > 0) {
                printReceivedAt("CLIENT", expectedAtClient, clientCh.lastStatus.dataReceived, clientCh.invalidData);
            }
        } else if (whoami == EndpointType::BRIDGE) {
            // we should have forwarded everything that CLIENT sent
            printReceivedAt("CLIENT -> BRIDGE", clientCh.lastStatus.dataSent, clientCh.received, clientCh.invalidData);
            // we should have forwarded everything that SERVER sent
            printReceivedAt("SERVER -> BRIDGE", serverCh.lastStatus.dataSent, serverCh.received, serverCh.invalidData);
        }

        if (whoami == EndpointType::SERVER || whoami == EndpointType::CLIENT) {
            traffic(whoami).receivedRange.printErrors();
        }
    }

    void printReceivedAt(const char* at, int32_t expected, int32_t actual, int32_t corrupted = 0) noexcept {
        int lost = expected - actual;
        float p = 100.0f * (float(actual) / std::max(expected,1));
        if      (p > 99.99f) LogInfo(GREEN( "   %s RECEIVED: %6.2f%% %5dpkts  LOST: %6.2f%% %dpkts"), at, p, actual, 100-p, lost);
        else if (p > 90.0f)  LogInfo(ORANGE("   %s RECEIVED: %6.2f%% %5dpkts  LOST: %6.2f%% %dpkts"), at, p, actual, 100-p, lost);
        else                 LogInfo(RED(   "   %s RECEIVED: %6.2f%% %5dpkts  LOST: %6.2f%% %dpkts"), at, p, actual, 100-p, lost);
        if (corrupted > 0) {
            LogInfo(RED("   %s RECEIVED CORRUPTED: %d packets"), at, corrupted);
        }
    }
};

int main(int argc, char *argv[])
{
    auto next_arg = [=](int* i) -> rpp::strview {
        if (++(*i) >= argc) printHelp(1);
        return argv[*i];
    };

    Args args;
    for (int i = 1; i < argc; ++i) {
        rpp::strview arg = argv[i];
        if (arg == "--listen" || arg == "--server") {
            args.is_server = true, args.is_bridge = false, args.is_client = false;
            args.listenerAddr = rpp::ipaddress4(next_arg(&i).to_int());
            if (!args.listenerAddr.is_valid()) {
                LogError("invalid listen port %d", args.listenerAddr.port());
                printHelp(1);
            }
        } else if (arg == "--client" || arg == "--connect" || arg == "--address") {
            args.is_server = false, args.is_bridge = false, args.is_client = true;
            args.serverAddr = rpp::ipaddress4(next_arg(&i));
            if (!args.serverAddr.is_valid()) {
                LogError("invalid server <ip:port>: '%s'", args.serverAddr.str());
                printHelp(1);
            }
        } else if (arg == "--bridge") {
            args.is_server = false, args.is_bridge = true, args.is_client = false;
            args.listenerAddr = rpp::ipaddress4(next_arg(&i).to_int());
            args.bridgeForwardAddr = rpp::ipaddress4(next_arg(&i));
            if (!args.listenerAddr.is_valid() || !args.bridgeForwardAddr.is_valid()) {
                LogError("invalid bridge port %d to <ip:port>: '%s'", args.listenerAddr.port(), args.bridgeForwardAddr.str());
                printHelp(1);
            }
        }
        else if (arg == "--size")     args.bytesPerBurst = parseSizeLiteral(next_arg(&i));
        else if (arg == "--rate")     args.bytesPerSec  = parseSizeLiteral(next_arg(&i));
        else if (arg == "--count")    args.count      = next_arg(&i).to_int();
        else if (arg == "--talkback") args.talkback   = parseSizeLiteral(next_arg(&i));
        else if (arg == "--buf")      args.rcvBufSize = args.sndBufSize = parseSizeLiteral(next_arg(&i));
        else if (arg == "--rcvbuf")   args.rcvBufSize = parseSizeLiteral(next_arg(&i));
        else if (arg == "--sndbuf")   args.sndBufSize = parseSizeLiteral(next_arg(&i));
        else if (arg == "--blocking")    args.blocking = true;
        else if (arg == "--nonblocking") args.blocking = false;
        else if (arg == "--echo")        args.echo = true;
        else if (arg == "--mtu") {
            args.mtu = next_arg(&i).to_int();
            if (args.mtu <= 0) {
                LogError("invalid mtu %d", args.mtu);
                printHelp(1);
            }
        }
        else if (arg == "--udpc") args.udpc = true;
        else if (arg == "--help") printHelp(0);
        else {
            LogError("unknown argument: %s", arg);
            printHelp(1);
        }
    }

    int modes = (args.is_server + args.is_client + args.is_bridge);
    if (modes == 0 || modes > 1) {
        printHelp(1);
    }

    // setup the connection
    UDPQuality udp { args };
    udp.c.create(args.blocking);
    if (args.is_server || args.is_bridge)
        udp.c.bind(args.listenerAddr.port());

    udp.c.balancer.set_max_bytes_per_sec(args.bytesPerSec);

    if (args.rcvBufSize == 0)
        LogInfo(CYAN("RCVBUF using OS default: %s"), toLiteral(udp.c.getBufSize(rpp::socket::BO_Recv)));
    else udp.c.setBufSize(rpp::socket::BO_Recv, args.rcvBufSize);

    if (args.sndBufSize == 0)
        LogInfo(CYAN("SNDBUF using OS default: %s"), toLiteral(udp.c.getBufSize(rpp::socket::BO_Send)));
    else udp.c.setBufSize(rpp::socket::BO_Send, args.sndBufSize);

    if (args.is_server) {
        LogInfo("\x1b[0mServer listening on port %d", args.listenerAddr.port());
        udp.server();
    } else if (args.is_client) {
        LogInfo("\x1b[0mClient connecting to server %s", args.serverAddr.str());
        udp.client();
    } else if (args.is_bridge) {
        LogInfo("\x1b[0mBridging on port %d to server %s", args.listenerAddr.port(), args.bridgeForwardAddr.str());
        udp.bridge();
    } else {
        printHelp(1);
    }
    return 0;
}
