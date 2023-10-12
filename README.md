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
Usage Server: ./udp_quality --listen <listen_port> --buf <socket_buf_size>
Usage Client: ./udp_quality --address <ip:port> --rate <bytes_per_sec> --buf <socket_buf_size>
Options:
    --listen <listen_port>   Server listens on this port
    --address <ip:port>      Client connects to this server
    --size <bytes_per_burst> Client sends this many bytes per burst [default 1MB]
    --rate <bytes_per_sec>   Client/Server rate limits, use 0 to disable [default]
    --buf <buf_size>         Socket SND/RCV buffer size [default 256KB]
    --sndbuf <snd_buf_size>  Socket SND buffer size [default 256KB]
    --rcvbuf <rcv_buf_size>  Socket RCV buffer size [default 256KB]
    --blocking               Uses blocking sockets [default]
    --nonblocking            Uses nonblocking sockets
    --udpc                   Uses alternative UDP C socket implementation
    --help

When running from ubuntu, sudo is required

    all rates can be expressed as a number followed by a unit:
        1000 = 1000 bytes
        1KB  = 1000 bytes
        1KiB = 1024 bytes
        1MB  = 1000 bytes
        1KiB = 1024 bytes
```
