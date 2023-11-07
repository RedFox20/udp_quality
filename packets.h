#pragma once
#include <stdint.h>

// 1000 is the default MTU size for our RTPh264 protocol
static constexpr int MTU_SIZE = 1000;

enum class PacketType : int8_t
{
    UNKNOWN = 0,
    DATA = 1,
    STATUS = 2
};

enum class StatusType : int8_t
{
    INIT = 0,
    FINISHED = 1,
    BURST_START = 2,
    BURST_FINISH = 3,
};

enum class EndpointType : int8_t
{
    UNKNOWN = 0,
    SERVER = 1,
    CLIENT = 2,
    BRIDGE = 3,
};

static const char* to_string(PacketType type) noexcept
{
    switch (type)
    {
        case PacketType::DATA:   return "DATA";
        case PacketType::STATUS: return "STATUS";
        default: return "UNKNOWN";
    }
}

static const char* to_string(StatusType type) noexcept
{
    switch (type)
    {
        case StatusType::INIT:         return "INIT";
        case StatusType::FINISHED:     return "FINISHED";
        case StatusType::BURST_START:  return "BURST_START";
        case StatusType::BURST_FINISH: return "BURST_FINISH";
        default: return "UNKNOWN";
    }
}

static const char* to_string(EndpointType type) noexcept
{
    switch (type)
    {
        case EndpointType::SERVER: return "Server";
        case EndpointType::CLIENT: return "Client";
        case EndpointType::BRIDGE: return "Bridge";
        default: return "UNKNOWN";
    }
}


struct Packet
{
    // DATA or STATUS?
    // if DATA, then additional data payload follows
    PacketType type = PacketType::UNKNOWN;
    StatusType status = StatusType::INIT;
    EndpointType sender = EndpointType::UNKNOWN;
    uint8_t echo = 0; // 0 or 1
    int32_t seqid = 0; // sequence id of this packet

    // length of this entire packet
    int32_t len = 0;

    // # which iteration of the test this is
    int32_t iteration = 0;

    // # of pkts CLIENT bursts to SERVER
    uint32_t burstCount = 0;

    // # of pkts SERVER bursts to CLIENT
    uint32_t talkbackCount = 0;

    // DATA packets sent by `sender`
    int32_t dataSent = 0;

    // DATA packets received by `sender`
    int32_t dataReceived = 0;

    // sets the load balancer bytes per second limit
    int32_t maxBytesPerSecond = 0;

    // sets the MTU size for the test
    int32_t mtu = 0;
};

// data packet with payload
// the payload is allocated dynamically, check Packet::len to get the size
struct Data : Packet
{
    char buffer[];

    int size() const noexcept { return size(this->len); }
    int size(int pktsize) const noexcept { return pktsize - sizeof(Packet); }
};
