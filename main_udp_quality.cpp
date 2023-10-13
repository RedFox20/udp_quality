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
    int32_t rcvBufSize = parseSizeLiteral("256KB");
    int32_t sndBufSize = parseSizeLiteral("256KB");
    int32_t bytesPerBurst = parseSizeLiteral("1MB");
    int32_t bytesPerSec = 0;
    int32_t count = 5;
    int32_t talkback = 0; // talkback packets to send back to server
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
    printf("    --buf <buf_size>         Socket SND/RCV buffer size [default 256KB]\n");
    printf("    --sndbuf <snd_buf_size>  Socket SND buffer size [default 256KB]\n");
    printf("    --rcvbuf <rcv_buf_size>  Socket RCV buffer size [default 256KB]\n");
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

        std::unordered_map<int32_t, PacketInfo> packets;
        PacketRange receivedRange;
        Packet lastStatus;
    };

    TrafficStatus clientCh; // traffic FROM / TO client
    TrafficStatus serverCh; // traffic FROM / TO server
    TrafficStatus unknownCh; // traffic FROM / TO unknown

    void reset(const Packet& clientInit) noexcept {
        args.echo = clientInit.echo != 0;
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
        Data data;
        data.type = PacketType::DATA;
        data.status = StatusType::BURST_START;
        data.sender = whoami;
        data.echo = args.echo;
        data.seqid = traffic(toWhom).sent;
        memset(data.buffer, 'A', sizeof(data.buffer));
        if (c.sendPacketTo(data, sizeof(data), toAddr))
            traffic(toWhom).sent++;
    }

    bool sendStatusPacket(StatusType status, const rpp::ipaddress& to) noexcept {
        Packet st;
        st.type = PacketType::STATUS;
        st.status = status;
        st.sender = whoami;

        st.echo = args.echo;
        st.seqid = statusSeqId++;
        st.iteration = statusIteration;
        st.burstCount = burstCount;
        st.talkbackCount = talkbackCount;

        st.dataSent = traffic(talkingTo).sent;
        st.dataReceived = traffic(talkingTo).received;
        st.maxBytesPerSecond = c.balancer.get_max_bytes_per_sec();
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

    void onDataReceived(Packet& p) noexcept {
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
        if (pktInfo.count > 1)
            tr.duplicatePackets++;
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
        burstCount = args.bytesPerBurst / DATA_PACKET_SIZE;
        if (args.talkback > 0) {
            talkbackCount = args.talkback / DATA_PACKET_SIZE;
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
            int32_t totalSize = DATA_PACKET_SIZE * burstCount;
            LogInfo(MAGENTA(">> SEND BURST pkts:%d  size:%s  rate:%s"), 
                    burstCount, toLiteral(totalSize), toRateLiteral(args.bytesPerSec));
            sendStatusPacket(StatusType::BURST_START, actualServer);
            traffic(talkingTo).receivedRange.reset();

            int32_t gotTalkback = 0;
            bool gotBurstFinish = false;

            auto handleRecv = [&](Packet& p) {
                if (p.type == PacketType::DATA) {
                    ++gotTalkback;
                    onDataReceived(p);
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
                int32_t expectedTalkbackBytes = numTalkback * DATA_PACKET_SIZE;
                int32_t minTalkbackMs = (expectedTalkbackBytes * 1000) / actualBytesPerSec;
                LogInfo(MAGENTA(">> WAITING TALKBACK %dms expected:%dpkts"), minTalkbackMs, numTalkback);
                waitAndRecvForDuration(minTalkbackMs);
            }

            rpp::sleep_ms(100);
            LogInfo(MAGENTA(">> SEND BURST FINISH recvd:%dpkts"), gotTalkback);
            // after we've waited enough, send BURST_FINISH
            if (!sendStatusPacket(StatusType::BURST_FINISH, actualServer))
                LogErrorExit(RED("Failed to send STATUS packet"));

            waitAndRecvForDuration(2000);
            if (!gotBurstFinish) {
                LogInfo(RED("timeout waiting BURST_FINISH ACK"));
            }

            if (statusIteration == args.count)
                break; // we're done
            ++statusIteration;
        }

        rpp::sleep_ms(100); // wait a bit, send finish and wait for FINISHED status
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
                onDataReceived(p);
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
                            talkbackCount, toLiteral(talkbackCount*DATA_PACKET_SIZE),
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
                onDataReceived(p);
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
            printReceivedAt("SERVER", /*expected*/serverCh.sent, /*actual*/serverCh.lastStatus.dataReceived);

            // we must know how many packets SERVER should send back to us
            int32_t expectedFromServer = (args.echo ? serverCh.sent : 0) + talkbackCount*iteration;
            if (expectedFromServer > 0) {
                printReceivedAt("CLIENT", expectedFromServer, serverCh.received);
            }
        } else if (whoami == EndpointType::SERVER) {
            // server must have received all the packets that client sent
            printReceivedAt("SERVER", /*expected*/clientCh.lastStatus.dataSent, /*actual*/clientCh.received);

            // client must have received all the packets that it sent + talkback
            int32_t expectedAtClient = 0;
            if (args.echo) expectedAtClient = clientCh.lastStatus.dataSent;
            if (talkbackCount > 0) expectedAtClient += talkbackCount*iteration;
            if (expectedAtClient > 0) {
                printReceivedAt("CLIENT", expectedAtClient, clientCh.lastStatus.dataReceived);
            }
        } else if (whoami == EndpointType::BRIDGE) {
            // we should have forwarded everything that CLIENT sent
            printReceivedAt("CLIENT -> BRIDGE", clientCh.lastStatus.dataSent, clientCh.received);
            // we should have forwarded everything that SERVER sent
            printReceivedAt("SERVER -> BRIDGE", serverCh.lastStatus.dataSent, serverCh.received);
        }

        if (whoami == EndpointType::SERVER || whoami == EndpointType::CLIENT) {
            traffic(whoami).receivedRange.printErrors();
        }
    }

    void printReceivedAt(const char* at, int32_t expected, int32_t actual) noexcept {
        int lost = expected - actual;
        float p = 100.0f * (float(actual) / std::max(expected,1));
        if      (p > 99.99f) LogInfo(GREEN( "   %s RECEIVED: %6.2f%% %4dpkts  LOST: %6.2f%% %dpkts"), at, p, actual, 100-p, lost);
        else if (p > 90.0f)  LogInfo(ORANGE("   %s RECEIVED: %6.2f%% %4dpkts  LOST: %6.2f%% %dpkts"), at, p, actual, 100-p, lost);
        else                 LogInfo(RED(   "   %s RECEIVED: %6.2f%% %4dpkts  LOST: %6.2f%% %dpkts"), at, p, actual, 100-p, lost);
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
        else if (arg == "--udpc")        args.udpc = true;
        else if (arg == "--help") printHelp(0);
        else                      printHelp(1);
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
    udp.c.setBufSize(rpp::socket::BO_Recv, args.rcvBufSize);
    udp.c.setBufSize(rpp::socket::BO_Send, args.sndBufSize);
    udp.c.balancer.set_max_bytes_per_sec(args.bytesPerSec);

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
