# Datagram sockets. recv_from records source address and port atomically with the
# packet and exposes them as one UdpPacket object.

object UdpPacket {
    public let data: bytes;
    public let host: str;
    public let port: int;

    public init(data: bytes, host: str, port: int) {
        self.data = data;
        self.host = host;
        self.port = port;
    }

    public fn text() -> str { return bytes_to_text(self.data); }
}

object UdpSocket {
    public let handle: int;

    public init(handle: int) { self.handle = handle; }

    public fn valid() -> bool { return self.handle > 0; }
    public fn port() -> int { return net_socket_local_port(self.handle); }
    public fn send_to(host: str, port: int, data: bytes, timeout_ms: int) -> int {
        return net_udp_send_to(self.handle, host, port, data, timeout_ms);
    }
    public fn send_text_to(host: str, port: int, data: str, timeout_ms: int) -> int {
        return self.send_to(host, port, bytes_from_text(data), timeout_ms);
    }
    public fn recv_from(max_bytes: int, timeout_ms: int) -> UdpPacket {
        let data = net_udp_recv_from(self.handle, max_bytes, timeout_ms);
        return UdpPacket(data, net_last_host(), net_last_port());
    }
    public fn wait_readable(timeout_ms: int) -> bool {
        return net_socket_wait_readable(self.handle, timeout_ms);
    }
    public fn close() -> bool { return net_socket_close(self.handle); }
}

fn udp_bind(host: str, port: int) -> UdpSocket {
    return UdpSocket(net_udp_bind(host, port));
}

async fn udp_recv_from_async(socket_handle: int, max_bytes: int, timeout_ms: int) -> UdpPacket {
    let data = net_udp_recv_from(socket_handle, max_bytes, timeout_ms);
    return UdpPacket(data, net_last_host(), net_last_port());
}
