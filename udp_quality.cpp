// UDP Quality Analysis Tool
// The server will listen for incoming UDP packets and simply echoes them back to the client.
// The server will also send back Status on how many packets it has received and sent
// The client will simply collect back the Status packets from the server
#include "udp_quality.h"
#include "simple_udp.h" // for --udpc option

#if __linux__
    #include <sys/socket.h>
#endif
#if _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <WinSock2.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "Ws2_32.lib") // link against winsock libraries
        #pragma comment(lib, "Iphlpapi.lib")
    #endif
#endif

#define MTU_SIZE 1000 // 1000 is the default MTU size for our RTPh264 protocol

struct Args
{
    int32_t rcvBufSize = parseSizeLiteral("256KB");
    int32_t sndBufSize = parseSizeLiteral("256KB");
    int32_t bytesPerBurst = parseSizeLiteral("1MB");
    int32_t bytesPerSec = 0;
    rpp::ipaddress4 listenerAddr;
    rpp::ipaddress serverAddr;
    bool blocking = true;
    bool udpc = false;
};

static Args args;

void printHelp(int exitCode) noexcept
{
    printf("Usage Server: ./udp_quality --listen <listen_port> --buf <socket_buf_size>\n");
    printf("Usage Client: ./udp_quality --address <ip:port> --rate <bytes_per_sec> --buf <socket_buf_size>\n");
    printf("Options:\n");
    printf("    --listen <listen_port>   Server listens on this port\n");
    printf("    --address <ip:port>      Client connects to this server\n");
    printf("    --size <bytes_per_burst> Client sends this many bytes per burst [default 1MB]\n");
    printf("    --rate <bytes_per_sec>   Client/Server rate limits, use 0 to disable [default]\n");
    printf("    --buf <buf_size>         Socket SND/RCV buffer size [default 256KB]\n");
    printf("    --sndbuf <snd_buf_size>  Socket SND buffer size [default 256KB]\n");
    printf("    --rcvbuf <rcv_buf_size>  Socket RCV buffer size [default 256KB]\n");
    printf("    --blocking               Uses blocking sockets [default]\n");
    printf("    --nonblocking            Uses nonblocking sockets\n");
    printf("    --udpc                   Uses alternative UDP C socket implementation\n");
    printf("    --help\n");
    printf("\n");
    printf("When running from ubuntu, sudo is required\n");
    printf("\n");
    printf("    all rates can be expressed as a number followed by a unit:\n");
    printf("        1000 = 1000 bytes\n");
    printf("        1KB  = 1000 bytes \n");
    printf("        1KiB = 1024 bytes\n");
    printf("        1MB  = 1000 bytes \n");
    printf("        1KiB = 1024 bytes\n");
    exit(1);
}

enum class PacketType : int32_t
{
    UNKNOWN = 0,
    DATA = 1,
    STATUS = 2
};

enum class StatusType : int32_t
{
    INIT = 0,
    FINISHED = 1,
    RUNNING = 2,
};

enum class SenderType : int32_t
{
    UNKNOWN = 0,
    SERVER = 1,
    CLIENT = 2,
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
        case StatusType::INIT:     return "INIT";
        case StatusType::FINISHED: return "FINISHED";
        case StatusType::RUNNING:  return "RUNNING";
        default: return "UNKNOWN";
    }
}

static const char* to_string(SenderType type) noexcept
{
    switch (type)
    {
        case SenderType::SERVER: return "Server";
        case SenderType::CLIENT: return "Client";
        default: return "UNKNOWN";
    }
}


struct Packet
{
    PacketType type = PacketType::DATA; // DATA or STATUS?
    int32_t seqid = 0; // sequence id of this packet
};

struct Status : Packet
{
    StatusType status;
    SenderType sender;

    // server: packets that we've echoed back
    // client: packets that we've initiated
    int32_t packetsSent;

    // server: packets that we've received from the client
    // client: packets that we've received back as the echo reply
    int32_t packetsReceived;

    // sets the load balancer bytes per second limit
    int32_t maxBytesPerSecond;
};

static constexpr int STATUS_PACKET_SIZE = sizeof(Status);
static constexpr int DATA_PACKET_SIZE = MTU_SIZE; // limit Header + Buffer to MTU size
static constexpr int DATA_BUFFER_SIZE = (DATA_PACKET_SIZE - sizeof(Packet));

struct Data : Packet
{
    char buffer[DATA_BUFFER_SIZE];
};


struct UDPQuality
{
    rpp::socket socket;
    int c_sock = -1; // simplified socket interface

    // rate limiter
    rpp::load_balancer balancer { uint32_t(8 * 1024 * 1024) };

    PacketRange range{};
    int32_t statusSeqId = 0;
    SenderType whoami = SenderType::SERVER;
    int32_t dataSent = 0;
    int32_t dataReceived = 0;
    char buffer[4096];

    UDPQuality() = default;
    ~UDPQuality() noexcept
    {
        if (use_rpp_socket()) socket.close();
        else                  socket_udp_close(c_sock);
    }

    bool use_rpp_socket() const noexcept { return c_sock == -1; }

    void sendDataPacket(const rpp::ipaddress& to) noexcept
    {
        Data data;
        data.type = PacketType::DATA;
        data.seqid = dataSent;
        memset(data.buffer, 'A', sizeof(data.buffer));
        if (sendPacketTo(&data, sizeof(data), to))
            ++dataSent;
    }

    bool sendStatusPacket(StatusType status, const rpp::ipaddress& to) noexcept
    {
        Status st;
        st.type = PacketType::STATUS;
        st.seqid = statusSeqId++;
        st.status = status;
        st.sender = whoami;
        st.packetsSent = dataSent;
        st.packetsReceived = dataReceived;
        st.maxBytesPerSecond = balancer.get_max_bytes_per_sec();
        printStatus("send", st);
        return sendPacketTo(&st, sizeof(Status), to);
    }

    void printStatus(const char* recvOrSend, const Status& st) noexcept
    {
        LogInfo("   %s from %s STATUS %d %s:   sent:%d recv:%d", recvOrSend,
                to_string(st.sender), st.seqid, to_string(st.status), st.packetsSent, st.packetsReceived);
    }

    bool sendPacketTo(const Packet* pkt, int pktlen, const rpp::ipaddress& to) noexcept
    {
        if (balancer.get_max_bytes_per_sec() != 0)
            balancer.wait_to_send(pktlen);

        int r = use_rpp_socket()
              ? socket.sendto(to, pkt, pktlen)
              : socket_sendto(c_sock, pkt, pktlen, to.Address.Addr4, to.Port);
        if (r <= 0) {
            LogError(RED("sendto %s %s len:%d failed: %s"), to.str(), to_string(pkt->type), pktlen, rpp::socket::last_os_socket_err());
            return false;
        }
        return true;
    }

    Status* recvStatusFrom(rpp::ipaddress& from, int timeoutMillis) noexcept
    {
        int received = recvPacketFrom(buffer, sizeof(buffer), from, timeoutMillis);
        if (received == 0) {
            LogError(RED("recv STATUS timeout"));
            return nullptr;
        }
        if (received < 0) {
            LogError(RED("recv STATUS failed: %s"), socket.last_err());
            return nullptr;
        }

        Packet& packet = *reinterpret_cast<Packet*>(buffer);
        if (packet.type != PacketType::STATUS) {
            LogError(RED("recv STATUS invalid packet.type:%d from: %s"), packet.type, socket.last_err());
            return nullptr;
        }
        Status* st = reinterpret_cast<Status*>(buffer);
        printStatus("recv", *st);
        return st;
    }

    int recvPacketFrom(char* buffer, int maxlen, rpp::ipaddress& from, int timeoutMillis) noexcept
    {
        if (timeoutMillis > 0)
        {
            bool canRead = use_rpp_socket()
                         ? socket.poll(timeoutMillis, rpp::socket::PF_Read)
                         : socket_poll_recv(c_sock, timeoutMillis);
            if (!canRead)
                return 0; // no data available (timeout)
        }
        int n = use_rpp_socket()
              ? socket.recvfrom(from, buffer, maxlen)
              : socket_recvfrom(c_sock, buffer, maxlen, &from.Address.Addr4, &from.Port);
        if (n <= 0) LogError("recvfrom failed: %s", rpp::socket::last_os_socket_err());
        if (n > 0) from.Address.Family = rpp::AF_IPv4;
        return n;
    }

    int getBufSize(rpp::socket::buffer_option buf) const noexcept
    {
        if (use_rpp_socket()) return socket.get_buf_size(buf);
        else                  return socket_get_buf_size(c_sock, (buf == rpp::socket::BO_Recv ? SO_RCVBUF : SO_SNDBUF));
    }

    bool setBufSize(rpp::socket::buffer_option buf, int bufSize) noexcept
    {
        const char* name = (buf == rpp::socket::BO_Recv ? "RCVBUF" : "SNDBUF");
        int finalSize;
        if (use_rpp_socket())
        {
            if (!socket.set_buf_size(buf, bufSize, /*force*/false))
                socket.set_buf_size(buf, bufSize, /*force*/true);
            finalSize = socket.get_buf_size(buf);
        }
        else
        {
            int so_buf = (buf == rpp::socket::BO_Recv ? SO_RCVBUF : SO_SNDBUF);
            socket_set_buf_size(c_sock, so_buf, bufSize);
            finalSize = socket_get_buf_size(c_sock, so_buf);
        }

        if (finalSize == bufSize)
            LogInfo(GREEN("set %s to %s SUCCEEDED"), name, toLiteral(bufSize));
        else
            LogError(RED("set %s to %s failed (remains %d): %s"),
                    name, toLiteral(bufSize), toLiteral(finalSize), rpp::socket::last_os_socket_err());
        return finalSize == bufSize;
    }

    std::string rateString(int bytesPerSec) const noexcept
    {
        return bytesPerSec > 0 ? toLiteral(bytesPerSec) + "/s" : "unlimited B/s";
    }

    void client() noexcept
    {
        whoami = SenderType::CLIENT;
        int32_t burstWriteCount = args.bytesPerBurst / DATA_PACKET_SIZE;
        const int iterations = 5;
        char buffer[4096];

        if (!sendStatusPacket(StatusType::INIT, args.serverAddr))
            LogErrorExit(RED("Failed to send INIT packet"));

        // and wait for response
        rpp::ipaddress actualServer;
        if (Status* st = recvStatusFrom(actualServer, /*timeoutMillis*/2000))
        {
            if (st->status != StatusType::INIT)
                LogErrorExit(RED("Handshake failed"));
            LogInfo(GREEN("Received HANDSHAKE: %s"), actualServer.str());
        }
        else LogErrorExit(RED("Handshake failed"));

        // only run a limited number of times
        for (int i = 0; i < iterations; ++i)
        {
            // send data burst
            int32_t totalSize = DATA_PACKET_SIZE * burstWriteCount;
            LogInfo(MAGENTA(">> SEND BURST pkts:%d  size:%s  rate:%s"), 
                    burstWriteCount, toLiteral(totalSize), rateString(args.bytesPerSec));

            rpp::Timer dataStart { rpp::Timer::AutoStart };
            for (int j = 0; j < burstWriteCount; ++j) {
                sendDataPacket(actualServer);
            }
            double dataElapsedMs = dataStart.elapsed_ms();
            double actualBytesPerSec = totalSize / (dataElapsedMs / 1000.0);
            LogInfo(MAGENTA(">> SEND ELAPSED %.2fms  actualrate:%s"), dataElapsedMs, rateString(int(actualBytesPerSec)));

            // after sending the packets, send the status
            if (!sendStatusPacket(StatusType::RUNNING, actualServer))
                LogErrorExit(RED("Failed to send STATUS packet"));

            // now collect all the data packets until we encounter a STATUS packet
            rpp::Timer start { rpp::Timer::AutoStart };
            range.reset();

            while (true)
            {
                if (start.elapsed_ms() >= 2000) {
                    LogInfo(RED("Timeout waiting data from SERVER. Our STATUS didn't arrive to server?"));
                    range.printErrors();
                    break;
                }

                int r = recvPacketFrom(buffer, sizeof(buffer), actualServer, /*timeoutMillis*/100);
                if (r == 0) continue; // no data available, continue waiting
                if (r < 0) {
                    range.printErrors();
                    break; // error
                }

                start.start(); // restart the timer
                Packet& pkt = *reinterpret_cast<Packet*>(buffer);
                if (pkt.type == PacketType::DATA)
                {
                    ++dataReceived;
                    range.push(pkt.seqid);
                }
                else if (pkt.type == PacketType::STATUS)
                {
                    Status& st = *reinterpret_cast<Status*>(buffer);
                    printStatus("recv", st);
                    range.printErrors();
                    if (dataSent != dataReceived)
                        LogInfo(ORANGE("Client DIDNT RECEIVE BACK %d packets"), dataSent - dataReceived);
                    break; // GOT STATUS, we're done
                }
            }
        }

        sendStatusPacket(StatusType::FINISHED, actualServer);
        rpp::sleep_ms(1000); // wait for all packets to be sent
        printSummary();
    }

    void printSummary()
    {
        float sent_rate = 100.0f * (float(dataReceived) / dataSent);
        float loss_rate = 100.0f - sent_rate;
        if      (sent_rate > 99.99f) LogInfo(GREEN( "Client SENT:     %.2f%%"), sent_rate);
        else if (sent_rate > 90.0f)  LogInfo(ORANGE("Client SENT:     %.2f%%"), sent_rate);
        else                         LogInfo(RED(   "Client SENT:     %.2f%%"), sent_rate);
        if      (loss_rate < 0.01f)  LogInfo(GREEN( "Client LOSS:     %.2f%%"), loss_rate);
        else if (loss_rate < 10.0f)  LogInfo(ORANGE("Client LOSS:     %.2f%%"), loss_rate);
        else                         LogInfo(RED(   "Client LOSS:     %.2f%%"), loss_rate);
        LogInfo("=============================================\n");
    }

    void server()
    {
        whoami = SenderType::SERVER;
        rpp::ipaddress clientAddr;
        range.reset();
        char buffer[4096];

        while (true) // receive packets infinitely
        {
            int32_t recvlen = recvPacketFrom(buffer, sizeof(buffer), clientAddr, /*timeoutMillis*/100);
            if (recvlen <= 0)
                continue;

            Packet* packet = reinterpret_cast<Packet*>(buffer);

            if ((packet->type != PacketType::DATA && packet->type != PacketType::STATUS) ||
                (packet->type == PacketType::DATA && recvlen != DATA_PACKET_SIZE) ||
                (packet->type == PacketType::STATUS && recvlen != STATUS_PACKET_SIZE))
            {
                LogInfo(ORANGE("recv invalid packet (size=%d) from %s: type=%d seqid=%d"),
                        recvlen, clientAddr.str(), int(packet->type), packet->seqid);
                continue;
            }

            if (packet->type == PacketType::DATA)
            {
                ++dataReceived;
                range.push(packet->seqid);
                if (sendPacketTo(packet, recvlen, clientAddr))
                    ++dataSent;
                else
                    LogInfo(ORANGE("Failed to echo packet: %d"), packet->seqid);
            }
            else if (packet->type == PacketType::STATUS)
            {
                Status& st = *reinterpret_cast<Status*>(buffer);
                // Client is initializing a new session
                if (st.status == StatusType::INIT)
                {
                    LogInfo("\x1b[0m===================================================");
                    dataReceived = 0;
                    dataSent = 0;
                    statusSeqId = 0;

                    printStatus("recv", st);
                    sendStatusPacket(StatusType::INIT, clientAddr); // echo back the init handshake

                    std::string rate = st.maxBytesPerSecond > 0 ? toLiteral(st.maxBytesPerSecond) + "/s" : "unlimited B/s";
                    LogInfo("   STARTED %d: %s  rate:%s  rcvbuf:%s  sndbuf:%s", 
                            st.seqid, clientAddr.str(), rate, 
                            toLiteral(getBufSize(rpp::socket::BO_Recv)),
                            toLiteral(getBufSize(rpp::socket::BO_Send)));
                    balancer.set_max_bytes_per_sec(st.maxBytesPerSecond);
                    range.reset();
                }
                else if (st.status == StatusType::FINISHED)
                {
                    LogInfo("|-------------------------------------------------|");
                    printStatus("recv", st);
                    sendStatusPacket(StatusType::FINISHED, clientAddr); // echo back the finished handshake
                    float sent_rate = 100.0f * (float(st.packetsReceived) / st.packetsSent);
                    float loss_rate = 100.0f - sent_rate;
                    if      (sent_rate > 99.99f) LogInfo(GREEN( "   Client SENT:     %.2f%%"), sent_rate);
                    else if (sent_rate > 90.0f)  LogInfo(ORANGE("   Client SENT:     %.2f%%"), sent_rate);
                    else                         LogInfo(RED(   "   Client SENT:     %.2f%%"), sent_rate);
                    if      (loss_rate < 0.01f)  LogInfo(GREEN( "   Client LOSS:     %.2f%%"), loss_rate);
                    else if (loss_rate < 10.0f)  LogInfo(ORANGE("   Client LOSS:     %.2f%%"), loss_rate);
                    else                         LogInfo(RED(   "   Client LOSS:     %.2f%%"), loss_rate);
                    LogInfo("===================================================");
                    LogInfo("");
                    range.printErrors();
                    range.reset();
                }
                else if (st.status == StatusType::RUNNING)
                {
                    LogInfo("|-------------------------------------------------|");
                    printStatus("recv", st);
                    sendStatusPacket(StatusType::RUNNING, clientAddr); // echo back status
                    range.printErrors();
                    range.reset();
                    if (st.packetsSent != dataReceived)
                        LogInfo(ORANGE("   Server DIDNT RECEIVE %d packets"), st.packetsSent - dataReceived);
                    if (dataSent != dataReceived)
                        LogInfo(ORANGE("   Server DIDNT ECHO %d packets"), dataReceived - dataSent);
                }
            }
        }
    }
};


int main(int argc, char *argv[])
{
    auto next_arg = [=](int* i) -> rpp::strview {
        if (++(*i) >= argc) printHelp(1);
        return argv[*i];
    };
    for (int i = 1; i < argc; ++i) {
        rpp::strview arg = argv[i];
        if      (arg == "--listen")  args.listenerAddr = rpp::ipaddress4(next_arg(&i).to_int());
        else if (arg == "--address") args.serverAddr   = rpp::ipaddress4(next_arg(&i));
        else if (arg == "--size")    args.bytesPerBurst = parseSizeLiteral(next_arg(&i));
        else if (arg == "--rate")    args.bytesPerSec  = parseSizeLiteral(next_arg(&i));
        else if (arg == "--buf")     args.rcvBufSize = args.sndBufSize = parseSizeLiteral(next_arg(&i));
        else if (arg == "--rcvbuf")  args.rcvBufSize = parseSizeLiteral(next_arg(&i));
        else if (arg == "--sndbuf")  args.sndBufSize = parseSizeLiteral(next_arg(&i));
        else if (arg == "--blocking")    args.blocking = true;
        else if (arg == "--nonblocking") args.blocking = false;
        else if (arg == "--udpc")        args.udpc = true;
        else if (arg == "--help") printHelp(0);
        else                      printHelp(1);
    }

    UDPQuality udp;

    bool is_server = !args.listenerAddr.is_empty();
    if (is_server && !args.listenerAddr.is_valid()) {
        LogError("invalid listen port %d", args.listenerAddr.port());
        printHelp(1);
    } else if (!is_server && !args.serverAddr.is_valid()) {
        LogError("Invalid server IP and port: '%s'", args.serverAddr.str());
        printHelp(1);
    }

    if (!args.udpc) {
        auto option = (args.blocking ? rpp::SO_Blocking : rpp::SO_NonBlock);
        if (!udp.socket.create(rpp::AF_IPv4, rpp::IPP_UDP, option))
            LogErrorExit("Error creating a socket");
        if (is_server && !udp.socket.bind(args.listenerAddr)) 
            LogErrorExit("server bind port=%d failed", args.listenerAddr.port());
    } else {
        udp.c_sock = socket_udp_create();
        if (udp.c_sock < 1)
            LogErrorExit("Error creating a socket");
        if (is_server && socket_udp_listener(udp.c_sock, args.listenerAddr.port()) != 0)
            LogErrorExit("server bind port=%d failed", args.listenerAddr.port());
        socket_set_blocking(udp.c_sock, args.blocking);
    }

    udp.setBufSize(rpp::socket::BO_Recv, args.rcvBufSize);
    udp.setBufSize(rpp::socket::BO_Send, args.sndBufSize);
    udp.balancer.set_max_bytes_per_sec(args.bytesPerSec);

    if (is_server) LogInfo("\x1b[0mServer listening on port %d", args.listenerAddr.port());
    else           LogInfo("\x1b[0mClient connecting to server %s", args.serverAddr.str());
    if (is_server) udp.server();
    else           udp.client();
    return 0;
}
