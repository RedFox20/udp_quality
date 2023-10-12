// UDP Quality Analysis Tool
// The server will listen for incoming UDP packets and simply echoes them back to the client.
// The server will also send back Status on how many packets it has received and sent
// The client will simply collect back the Status packets from the server
#include "udp_quality.h"
#include "socket_udp.h" // for --udpc option

#define MTU_SIZE 1000 // 1000 is the default MTU size for our RTPh264 protocol

struct Args
{
    int32_t bufSize = parseBytes("256KB");
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
    printf("    --listen <listen_port>  Server listens on this port\n");
    printf("    --address <ip:port>     Client connects to this server\n");
    printf("    --rate <bytes_per_sec>  Client/Server rate limits, use 0 to disable [default]\n");
    printf("    --buf <socket_buf_size> Socket buffer size [default 256KB]\n");
    printf("    --blocking              Uses blocking sockets [default]\n");
    printf("    --nonblocking           Uses nonblocking sockets\n");
    printf("    --udpc                  Uses alternative UDP C socket implementation\n");
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
    DATA = 0,
    STATUS = 1
};

enum class StatusType : int32_t
{
    INIT = 0,
    FINISHED = 1,
    RUNNING = 2,
};

struct Status
{
    StatusType type;

    // server: packets that we've echoed back
    // client: packets that we've initiated
    int32_t packetsSent;

    // server: packets that we've received from the client
    // client: packets that we've received back as the echo reply
    int32_t packetsReceived;

    // sets the load balancer bytes per second limit
    int32_t maxBytesPerSecond;
} ;

struct Header
{
    PacketType type = PacketType::DATA; // DATA or STATUS?
    int32_t len = 0; // size of this packet
    int32_t seqid = 0; // sequence id of this packet
};

#define STATUS_PACKET_SIZE (sizeof(Status) + sizeof(Header))
#define DATA_PACKET_SIZE (MTU_SIZE)
#define DATA_BUFFER_SIZE (DATA_PACKET_SIZE - sizeof(Header))

struct Packet
{
    Header header;
    union {
        Status status;
        char buffer[DATA_BUFFER_SIZE];
    };
};

struct UDPQuality
{
    rpp::socket socket;
    int c_sock = -1;
    rpp::load_balancer balancer { uint32_t(8 * 1024 * 1024) };
    int32_t socketBufSize = 0;

    UDPQuality() = default;
    ~UDPQuality() noexcept
    {
        if (use_rpp_socket())
            socket.close();
        else
        {
        #if _WIN32
            closesocket(c_sock);
        #else
            close(c_sock)
        #endif
        }
    }

    bool use_rpp_socket() const noexcept { return c_sock == -1; }

    void sendDataPacket(const rpp::ipaddress& to, int32_t* packetsSent) noexcept
    {
        Packet pkt;
        pkt.header.type = PacketType::DATA;
        pkt.header.len = DATA_PACKET_SIZE;
        pkt.header.seqid = *packetsSent;
        memset(pkt.buffer, 'A', sizeof(pkt.buffer));
        if (sendPacketTo(pkt, to))
            ++(*packetsSent);
    }

    void sendStatusPacket(StatusType type, const rpp::ipaddress& to, int32_t packetsSent, int32_t packetsReceived) noexcept
    {
        Packet pkt;
        pkt.header.type = PacketType::STATUS;
        pkt.header.len = STATUS_PACKET_SIZE;
        pkt.header.seqid = packetsSent;
        pkt.status.type = type;
        pkt.status.packetsSent = packetsSent;
        pkt.status.packetsReceived = packetsReceived;
        pkt.status.maxBytesPerSecond = balancer.get_max_bytes_per_sec();
        sendPacketTo(pkt, to);
    }

    bool sendPacketTo(const Packet& pkt, const rpp::ipaddress& to) noexcept
    {
        if (balancer.get_max_bytes_per_sec() != 0)
            balancer.wait_to_send(pkt.header.len);

        if (use_rpp_socket())
        {
            if (socket.sendto(to, &pkt, pkt.header.len) <= 0)
            {
                LogError(RED("sendto %s %d failed: %s"), to.str(), pkt.header.len, socket.last_err());
                return false;
            }
        }
        else
        {
            struct sockaddr_in addr_to;
            addr_to.sin_family = AF_INET;
            addr_to.sin_port = htons(to.Port);
            addr_to.sin_addr.s_addr = to.Address.Addr4;
            memset(addr_to.sin_zero, 0, sizeof(addr_to.sin_zero));
            int r = sendto(c_sock, (const char*)&pkt, pkt.header.len, 0, (struct sockaddr*)&addr_to, sizeof(addr_to));
            if (r <= 0)
            {
                LogError(RED("sendto %s %d failed: %s"), to.str(), pkt.header.len, rpp::socket::last_os_socket_err());
                return false;
            }
        }
        return true;
    }

    int recvPacketFrom(Packet* pkt, rpp::ipaddress& from, int timeoutMillis) noexcept
    {
        if (timeoutMillis > 0)
        {
            if (use_rpp_socket())
            {
                if (!socket.poll(timeoutMillis))
                    return 0; // no data available (timeout)
            }
            else
            {
                struct pollfd pfd;
                pfd.fd = c_sock;
                pfd.events = POLLIN;
                pfd.revents = 0;
                #if _WIN32 || _WIN64
                    int r = WSAPoll(&pfd, 1, timeoutMillis);
                #else
                    int r = ::poll(&pfd, 1, timeoutMillis);
                #endif
                if (r <= 0 || (pfd.revents & POLLIN) == 0)
                    return 0; // no data available (timeout)
            }
        }

        int n;
        if (use_rpp_socket())
        {
            n = socket.recvfrom(from, pkt, sizeof(*pkt));
            if (n <= 0)
            {
                LogError("recvfrom failed: %s", socket.last_err());
            }
        }
        else
        {
            struct sockaddr_in addr_from;
            socklen_t len = sizeof(addr_from);
            n = recvfrom(c_sock, (char*)pkt, sizeof(*pkt), 0, (struct sockaddr*)&addr_from, &len);
            if (n <= 0)
            {
                LogError("recvfrom failed: %s", rpp::socket::last_os_socket_err());
            }
            else
            {
                from.Address.Family = rpp::AF_IPv4;
                from.Address.Addr4 = addr_from.sin_addr.s_addr;
                from.Port = ntohs(addr_from.sin_port);
            }
        }
        return n;
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
            int command = (buf == rpp::socket::BO_Recv ? SO_RCVBUF : SO_SNDBUF);
        #if __linux__
            // NOTE: on linux the kernel doubles buffsize for internal bookkeeping
            //       so to keep things consistent between platforms, we divide by 2 on linux:
            int size_cmd = static_cast<int>(bufSize / 2);
            int force_command = (opt == BO_Recv ? SO_RCVBUFFORCE : SO_SNDBUFFORCE);
        #else
            int size_cmd = static_cast<int>(bufSize);
            int force_command = 0;
        #endif
            bool ok = setsockopt(c_sock, SOL_SOCKET, command, (char*)&size_cmd, sizeof(int)) == 0;
            if (!ok && force_command != 0)
                setsockopt(c_sock, SOL_SOCKET, force_command, (char*)&size_cmd, sizeof(int));
            socklen_t len = sizeof(int);
            getsockopt(c_sock, SOL_SOCKET, command, (char*)&finalSize, &len);
        }

        if (finalSize == bufSize)
        {
            LogInfo(GREEN("set %s to %s SUCCEEDED"), name, toLiteral(bufSize));
            return true;
        }
        else
        {
            LogError(RED("set %s to %s failed (remains %d): %s"),
                    name, toLiteral(bufSize), toLiteral(finalSize), rpp::socket::last_os_socket_err());
            return false;
        }
    }

    void setSocketBufSize(int32_t bufSize)
    {
        if (socketBufSize == bufSize)
            return;
        socketBufSize = bufSize;
        setBufSize(rpp::socket::BO_Recv, bufSize);
        setBufSize(rpp::socket::BO_Send, bufSize);
    }

    void client(int32_t bytesPerSec, const rpp::ipaddress& serverAddr) noexcept
    {
        int32_t packetsSent = 0;
        int32_t packetsReceived = 0;
        int32_t burstWriteCount = bytesPerSec / DATA_PACKET_SIZE;
        const int iterations = 5;

        // initialize test with [0,0]
        LogInfo("Sending handshake to %s", serverAddr.str());
        sendStatusPacket(StatusType::INIT, serverAddr, 0, 0);

        // and wait for response
        rpp::ipaddress actualServer;
        Packet pkt;
        int initResponse = recvPacketFrom(&pkt, actualServer, /*timeoutMillis*/2000);
        if (initResponse == 0) LogErrorExit(RED("Handshake failed (timeout)"));
        if (initResponse < 0) LogErrorExit(RED("Handshake failed: %s"), socket.last_err());

        if (pkt.status.type != StatusType::INIT)
            LogErrorExit(RED("Invalid handshake (%d) from: %s"), pkt.status.type, socket.last_err());

        LogInfo("Received handshake from server: %s", actualServer.str());

        // only run a limited number of times
        for (int i = 0; i < iterations; ++i)
        {
            // send data burst
            LogInfo(BLUE("Burst writing %d packets, total %s/s"), burstWriteCount, toLiteral(bytesPerSec));
            for (int j = 0; j < burstWriteCount; ++j)
            {
                sendDataPacket(actualServer, &packetsSent);
            }

            // send status
            LogInfo(BLUE("Send STATUS to server: %d sent, %d received"), packetsSent, packetsReceived);
            sendStatusPacket(StatusType::RUNNING, actualServer, packetsSent, packetsReceived);

            // receive echoed packets UNTIL we receive a status packet or until we time out
            rpp::Timer start;
            PacketRange range = { 0, { 0 } };

            LogInfo(BLUE("Waiting until STATUS response from server"));
            int statusResponse = recvPacketFrom(&pkt, actualServer, /*timeoutMillis*/2000);
            if (statusResponse <= 0)
                LogInfo(RED("Timeout waiting for server STATUS response. Our STATUS didn't arrive to server?"));

            while (true)
            {
                uint32_t elapsed_ms = start.elapsed_ms();
                if (elapsed_ms >= 5000)
                {
                    LogInfo(RED("Timeout waiting for server STATUS response. Our STATUS didn't arrive to server?"));
                    range.printErrors();
                    break;
                }

                int r = recvPacketFrom(&pkt, actualServer, /*timeoutMillis*/50);
                if (r == 0)
                    continue; // no data available, continue waiting

                if (r < 0)
                {
                    range.printErrors();
                    break; // error
                }

                if (pkt.header.type == PacketType::DATA)
                {
                    ++packetsReceived;
                    range.push(pkt.header.seqid);
                }
                else if (pkt.header.type == PacketType::STATUS)
                {
                    range.printErrors();
                    LogInfo("=============================================");
                    LogInfo("Server STATUS:   %d received", pkt.status.packetsReceived);
                    LogInfo("Client STATUS:   %d sent, %d received back", packetsSent, packetsReceived);
                    if (packetsSent != packetsReceived)
                        LogInfo(ORANGE("Client DIDNT RECEIVE BACK %d packets"), packetsSent - packetsReceived);

                    float sent_rate = 100.0f * (float(packetsReceived) / packetsSent);
                    float loss_rate = 100.0f - sent_rate;
                    if      (sent_rate > 99.99f) LogInfo(GREEN( "Client SENT:     %.2f%%"), sent_rate);
                    else if (sent_rate > 90.0f)  LogInfo(ORANGE("Client SENT:     %.2f%%"), sent_rate);
                    else                         LogInfo(RED(   "Client SENT:     %.2f%%"), sent_rate);
                    if      (loss_rate < 0.01f)  LogInfo(GREEN( "Client LOSS:     %.2f%%"), loss_rate);
                    else if (loss_rate < 10.0f)  LogInfo(ORANGE("Client LOSS:     %.2f%%"), loss_rate);
                    else                         LogInfo(RED(   "Client LOSS:     %.2f%%"), loss_rate);
                    LogInfo("=============================================\n");
                    break; // GOT STATUS, write next batch
                }
            }
        }

        sendStatusPacket(StatusType::FINISHED, actualServer, packetsSent, packetsReceived);
        rpp::sleep_ms(1000); // wait for all packets to be sent
    }

    void server()
    {
        int packetsReceived = 0; // server packetsSent always equals packetsReceived
        PacketRange range = { 0, { 0 } };
        rpp::ipaddress clientAddr;

        while (true) // receive packets infinitely
        {
            Packet pkt;
            int32_t numReceived = recvPacketFrom(&pkt, clientAddr, /*timeoutMillis*/50);
            if (numReceived <= 0)
                continue;

            if (pkt.header.len == 0)
            {
                LogInfo(ORANGE("Received empty packet (size=%d) from %s: type=%d len=%d seqid=%d"),
                        numReceived, clientAddr.str(), int(pkt.header.type), pkt.header.len, pkt.header.seqid);
                continue;
            }

            if (pkt.header.type == PacketType::DATA)
            {
                ++packetsReceived;
                range.push(pkt.header.seqid);
                sendPacketTo(pkt, clientAddr); // echo back the packet
            }
            else if (pkt.header.type == PacketType::STATUS)
            {
                // Client is initializing a new session
                if (pkt.status.type == StatusType::INIT)
                {
                    LogInfo("\x1b[0m=============================================");
                    std::string rate = pkt.status.maxBytesPerSecond > 0
                                     ? toLiteral(pkt.status.maxBytesPerSecond) + "/s"
                                     : "unlimited B/s";
                    LogInfo("Client STARTED: %s  rate:%s  buf:%s\n", clientAddr.str(), rate, toLiteral(socketBufSize));
                    packetsReceived = 0;
                    balancer.set_max_bytes_per_sec(pkt.status.maxBytesPerSecond);
                    range.reset();
                    sendStatusPacket(StatusType::INIT, clientAddr, 0, 0); // echo back the init handshake
                }
                else if (pkt.status.type == StatusType::FINISHED)
                {
                    LogInfo("=============================================");
                    LogInfo("Client FINISHED: %s", clientAddr.str());
                    LogInfo("Client STATUS:   %d sent, %d received", pkt.status.packetsSent, pkt.status.packetsReceived);
                    float sent_rate = 100.0f * (float(pkt.status.packetsReceived) / pkt.status.packetsSent);
                    float loss_rate = 100.0f - sent_rate;
                    if      (sent_rate > 99.99f) LogInfo(GREEN( "Client SENT:     %.2f%%"), sent_rate);
                    else if (sent_rate > 90.0f)  LogInfo(ORANGE("Client SENT:     %.2f%%"), sent_rate);
                    else                         LogInfo(RED(   "Client SENT:     %.2f%%"), sent_rate);
                    if      (loss_rate < 0.01f)  LogInfo(GREEN( "Client LOSS:     %.2f%%"), loss_rate);
                    else if (loss_rate < 10.0f)  LogInfo(ORANGE("Client LOSS:     %.2f%%"), loss_rate);
                    else                         LogInfo(RED(   "Client LOSS:     %.2f%%"), loss_rate);
                    LogInfo("=============================================");
                    LogInfo("");
                    range.printErrors();
                    range.reset();
                    sendStatusPacket(StatusType::FINISHED, clientAddr, 0, 0); // echo back the finished handshake
                }
                else
                {
                    range.printErrors();
                    range.reset();
                    LogInfo("=============================================");
                    LogInfo("Client STATUS:   %d sent, %d received", pkt.status.packetsSent, pkt.status.packetsReceived);
                    LogInfo("Server STATUS:   %d received & echoed", packetsReceived);
                    if (pkt.status.packetsSent != packetsReceived)
                        LogInfo(ORANGE("Server DIDNT RECEIVE %d packets"), pkt.status.packetsSent - packetsReceived);
                    sendStatusPacket(StatusType::RUNNING, clientAddr, packetsReceived, packetsReceived); // echo back status
                }
            }
        }
    }
};


int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        rpp::strview arg = argv[i];
        if (arg.equals("--listen"))
        {
            if (++i >= argc) printHelp(1);
            args.listenerAddr = rpp::ipaddress4(atoi(argv[i]));
        }
        else if (arg.equals("--address"))
        {
            if (++i >= argc) printHelp(1);
            args.serverAddr = rpp::ipaddress4(argv[i]);
        }
        else if (arg.equals("--rate"))
        {
            if (++i >= argc) printHelp(1);
            args.bytesPerSec = parseBytes(argv[i]);
        }
        else if (arg.equals("--buf"))
        {
            if (++i >= argc) printHelp(1);
            args.bufSize = parseBytes(argv[i]);
        }
        else if (arg == "--blocking")    args.blocking = true;
        else if (arg == "--nonblocking") args.blocking = false;
        else if (arg.equals("--udpc")) args.udpc = true;
        else if (arg.equals("--help")) printHelp(0);
        else                           printHelp(1);
    }

    UDPQuality udp;

    bool is_server = !args.listenerAddr.is_empty();
    if (is_server && !args.listenerAddr.is_valid())
    {
        LogError("invalid listen port %d", args.listenerAddr.port());
        printHelp(1);
    }
    else if (!is_server && !args.serverAddr.is_valid())
    {
        LogError("Invalid server IP and port: '%s'", args.serverAddr.str());
        printHelp(1);
    }

    if (!args.udpc)
    {
        auto option = (args.blocking ? rpp::SO_Blocking : rpp::SO_NonBlock);
        if (!udp.socket.create(rpp::AF_IPv4, rpp::IPP_UDP, option))
            LogErrorExit("Error creating a socket");

        if (is_server && !udp.socket.bind(args.listenerAddr)) 
            LogErrorExit("server bind port=%d failed", args.listenerAddr.port());
    }
    else
    {
        #if _WIN32
            WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
        #endif

        udp.c_sock = socket_udp_create();
        if (udp.c_sock < 1) LogErrorExit("Error creating a socket");

        if (is_server)
        {
            std::string port = std::to_string(args.listenerAddr.port());
            if (socket_udp_bind(udp.c_sock, NULL, port.c_str()) != 0)
                LogErrorExit("server bind port=%s failed", port);
        }
        else
        {
            std::string ip = args.serverAddr.address().str();
            std::string port = std::to_string(args.serverAddr.port());
            if (socket_udp_bind(udp.c_sock, ip.c_str(), port.c_str()) != 0)
                LogErrorExit("client bind address=%s failed", port);
        }

    #if _WIN32
        u_long val = args.blocking?0:1; // FIONBIO: !=0 nonblock, 0 block
        if (ioctlsocket(udp.c_sock, FIONBIO, &val) != 0)
            LogErrorExit("Error setting socket to nonblocking");
    #else
        int flags = fcntl(Sock, F_GETFL, 0);
        if (flags < 0) flags = 0;
        flags = args.blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
        if (fcntl(Sock, F_SETFL, flags) != 0)
            LogErrorExit("Error setting socket to nonblocking");
    #endif
    }

    if (is_server)
        LogInfo("\x1b[0mServer listening on port %d", args.listenerAddr.port());
    else
        LogInfo("\x1b[0mClient binding to server %s", args.serverAddr.str());

    udp.setSocketBufSize(args.bufSize);
    udp.balancer.set_max_bytes_per_sec(args.bytesPerSec);

    if (is_server)
        udp.server();
    else
        udp.client(args.bytesPerSec, args.serverAddr);
    return 0;
}
