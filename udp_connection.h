#pragma once
#include "logging.h"
#include "simple_udp.h"
#include "packets.h"
#include <rpp/sockets.h>

/**
 * Abstract UDP connection
 */
struct UDPConnection
{
    rpp::socket socket;
    int c_sock = -1; // simplified socket interface
    bool useRpp;

    // rate limiter
    rpp::load_balancer balancer { uint32_t(8 * 1024 * 1024) };
    char buffer[4096];

    explicit UDPConnection(bool useRpp) noexcept : useRpp{useRpp} {}

    ~UDPConnection() noexcept
    {
        if (useRpp) socket.close();
        else        socket_udp_close(c_sock);
    }

    int32_t getRateLimit() const noexcept { return balancer.get_max_bytes_per_sec(); }

    void create(bool blocking) noexcept
    {
        if (useRpp) {
            auto option = (blocking ? rpp::SO_Blocking : rpp::SO_NonBlock);
            if (!socket.create(rpp::AF_IPv4, rpp::IPP_UDP, option))
                LogErrorExit("error creating UDP socket");
        } else {
            c_sock = socket_udp_create();
            if (c_sock < 1) LogErrorExit("error creating UDP socket");
            socket_set_blocking(c_sock, blocking);
        }
    }

    void bind(int localPort) noexcept
    {
        if (useRpp ? !socket.bind(rpp::ipaddress4{localPort})
                   : socket_udp_listener(c_sock, localPort) != 0)
            LogErrorExit("server bind port=%d failed", localPort);
    }

    bool sendPacketTo(const Packet& pkt, int pktlen, const rpp::ipaddress& to) noexcept
    {
        if (balancer.get_max_bytes_per_sec() != 0)
            balancer.wait_to_send(pktlen);

        int r = useRpp
              ? socket.sendto(to, &pkt, pktlen)
              : socket_sendto(c_sock, &pkt, pktlen, to.Address.Addr4, to.Port);
        if (r <= 0) {
            LogError(RED("sendto %s %s len:%d failed: %s"), to.str(), to_string(pkt.type), pktlen, rpp::socket::last_os_socket_err());
            return false;
        }
        return true;
    }

    Packet& getReceivedPacket() noexcept { return *reinterpret_cast<Packet*>(buffer); }

    bool pollRead(int timeoutMillis = 0) const noexcept
    {
        return useRpp ? socket.poll(timeoutMillis, rpp::socket::PF_Read)
                      : socket_poll_recv(c_sock, timeoutMillis);
    }

    Packet* tryRecvPacket(int timeoutMillis = 0) noexcept
    {
        rpp::ipaddress from;
        int r = recvPacketFrom(from, /*timeoutMillis*/timeoutMillis);
        return r > 0 ? &getReceivedPacket() : nullptr;
    }

    int recvPacketFrom(rpp::ipaddress& from, int timeoutMillis) noexcept
    {
        if (timeoutMillis >= 0 && !pollRead(timeoutMillis))
            return 0; // no data available (timeout)

        rpp::ipaddress sentFrom;
        int r;
        if (useRpp) {
            r = socket.recvfrom(sentFrom, buffer, sizeof(buffer));
        } else {
            sentFrom.Address.Family = rpp::AF_IPv4;
            r = socket_recvfrom(c_sock, buffer, sizeof(buffer), &sentFrom.Address.Addr4, &sentFrom.Port);
        }

        if (r <= 0) {
            if (rpp::socket::last_os_socket_err_type() == rpp::socket::SE_CONNRESET)
                return r; // ignore connection reset errors
            LogError("recvfrom failed: %s", rpp::socket::last_os_socket_err());
            return r;
        }

        // validate the packet
        Packet& p = getReceivedPacket();
        if ((p.type != PacketType::DATA && p.type != PacketType::STATUS) ||
            (p.type == PacketType::DATA && r != DATA_PACKET_SIZE) ||
            (p.type == PacketType::STATUS && r != STATUS_PACKET_SIZE)) {
            LogInfo(ORANGE("recv invalid packet (size=%d) from %s: type=%d seqid=%d"),
                    r, sentFrom.str(), int(p.type), p.seqid);
            return -1;
        }

        // packet is OK, set `from`
        from = sentFrom;
        return r;
    }

    int getBufSize(rpp::socket::buffer_option buf) const noexcept
    {
        if (useRpp) return socket.get_buf_size(buf);
        else        return socket_get_buf_size(c_sock, (buf == rpp::socket::BO_Recv));
    }

    bool setBufSize(rpp::socket::buffer_option buf, int bufSize) noexcept
    {
        const char* name = (buf == rpp::socket::BO_Recv ? "RCVBUF" : "SNDBUF");
        int finalSize;
        if (useRpp) {
            if (!socket.set_buf_size(buf, bufSize, /*force*/false))
                socket.set_buf_size(buf, bufSize, /*force*/true);
            finalSize = socket.get_buf_size(buf);
        } else {
            socket_set_buf_size(c_sock, (buf == rpp::socket::BO_Recv), bufSize);
            finalSize = socket_get_buf_size(c_sock, (buf == rpp::socket::BO_Recv));
        }

        if (finalSize == bufSize)
            LogInfo(GREEN("set %s to %s SUCCEEDED"), name, toLiteral(bufSize));
        else
            LogError(RED("set %s to %s failed (remains %s): %s"),
                    name, toLiteral(bufSize), toLiteral(finalSize), rpp::socket::last_os_socket_err());
        return finalSize == bufSize;
    }
};
