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
