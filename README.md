# UDP Quality #
A tool for testing UDP network quality in high packet rate scenarios

## Building ##
This can be built with cmake or with mamabuild.
```
mama build
```

Start server
```
mama start="--listener 7777 --buf 256KB"
```

Start client
```
mama start="--address ip.address.here:7777 --rate 1MB --buf 256KB"
```

## Usage Help ##
```
UDP Quality Tool v1.0 - (c) 2023 KrattWorks
Usage Client: ./udp_quality --client <ip:port> --size <burst_size> --rate <bytes_per_sec> --buf <socket_buf_size>
Usage Server: ./udp_quality --listen <listen_port> --buf <socket_buf_size>
Usage Bridge: ./udp_quality --bridge <listen_port> <to_ip> --buf <socket_buf_size>
Details:
    Client controls the main parameters of the test: --rate and --size
    Server and Bridge only control their own socket buffer size: --buf
    If Server and Bridge set their own --rate then it will override client
Options:
    --listen <listen_port>   Server listens on this port
    --client <ip:port>       Client connects to this server
    --bridge <listen_port> <to_ip> Bridge listens on port and forwards to_ip
    --rate <bytes_per_sec>   Client/Server rate limits, use 0 to disable [default unlimited]
    --size <bytes>           Client sends this many bytes per burst [default 1MB]
    --count <iterations>     Client/Server runs this many iterations [default 5]
    --talkback <bytes>       Server sends this many bytes on its own [default 0]
    --echo                   Server will also echo all recvd data packets [default false]
    --buf <buf_size>         Socket SND/RCV buffer size [default: OS configured]
    --sndbuf <snd_buf_size>  Socket SND buffer size [default: OS configured]
    --rcvbuf <rcv_buf_size>  Socket RCV buffer size [default: OS configured]
    --blocking               Uses blocking sockets [default]
    --nonblocking            Uses nonblocking sockets
    --udpc                   Uses alternative UDP C socket implementation
    --help
  When running from ubuntu, sudo is required
  All rates can be expressed as a number followed by a unit:
        1000 = 1000 bytes   1KB  = 1000 bytes   1MB  = 1000*1000 bytes
                            1KiB = 1024 bytes   1MiB = 1024*1024 bytes
```

IP address information and tools
```
#CV25:             172.16.223.20
#GROUNDSTATION:    172.16.223.10
# CV25 eth0
ifconfig eth0 172.16.223.20 netmask 255.255.255.0

# These can be used to configure LAN settings on CV25
tc qdisc show dev eth0
tc qdisc del dev eth0 root
tc qdisc add dev eth0 root tbf rate 20mbit burst 32kb latency 400ms
ethtool -s eth0 autoneg on speed 10 duplex full
ethtool -s eth0 autoneg on speed 100 duplex full
```

Useful ways to run the tool:
```
SERVER (typically CV25)
    HILINK/CV25: udp_quality --server 9999 --buf 1000KB
    DESKTOP/WSL: mama build start="--server 9999 --buf 1000KB"

CLIENT (typically Windows or WSL)
    DESKTOP:   mama start="--client 192.168.1.52:8888 --size 1000KB --talkback 1000KB --rate 1000KB --buf 1000KB"
               mama start="--client 127.0.0.1 --count 5 --size 500KB --talkback 100KB --rate 500KB"

    CV25:       udp_quality --client 172.16.223.15:8888 --size 5000KB --talkback 50KB --rate 0KB
                udp_quality --client 172.16.223.19:8888 --count 5 --size 500KB --talkback 100KB --rate 500KB
    CV25_TO_PC:     udp_quality --client 172.16.223.12:9999 --size 5000KB --talkback 50KB --rate 0KB

BRIDGE (if you need to bridge between several networks)
    # --bridge <listen_port> <forward_to_server_ip>
    udp_quality --bridge 8888 172.16.223.20:9999 --buf 1000KB
    mama start="--bridge 8888 172.16.223.20:9999 --buf 1000KB"
```
