# Portable TCP sockets. Runtime handles are nonblocking internally; every
# operation takes a timeout and observes task cancellation while waiting.

object TcpStream {
    public let handle: int;

    public init(handle: int) { self.handle = handle; }

    public fn valid() -> bool { return self.handle > 0; }
    public fn peer_host() -> str { return net_socket_peer_host(self.handle); }
    public fn peer_port() -> int { return net_socket_peer_port(self.handle); }
    public fn local_port() -> int { return net_socket_local_port(self.handle); }
    public fn set_nodelay(enabled: bool) -> bool {
        return net_socket_set_nodelay(self.handle, enabled);
    }
    public fn wait_readable(timeout_ms: int) -> bool {
        return net_socket_wait_readable(self.handle, timeout_ms);
    }
    public fn wait_writable(timeout_ms: int) -> bool {
        return net_socket_wait_writable(self.handle, timeout_ms);
    }
    public fn send(data: bytes, timeout_ms: int) -> int {
        return net_socket_send(self.handle, data, timeout_ms);
    }
    public fn send_text(data: str, timeout_ms: int) -> int {
        return net_socket_send(self.handle, bytes_from_text(data), timeout_ms);
    }
    public fn recv(max_bytes: int, timeout_ms: int) -> bytes {
        return net_socket_recv(self.handle, max_bytes, timeout_ms);
    }
    public fn recv_text(max_bytes: int, timeout_ms: int) -> str {
        return bytes_to_text(self.recv(max_bytes, timeout_ms));
    }
    public fn shutdown_read() -> bool { return net_socket_shutdown(self.handle, 0); }
    public fn shutdown_write() -> bool { return net_socket_shutdown(self.handle, 1); }
    public fn shutdown() -> bool { return net_socket_shutdown(self.handle, 2); }
    public fn close() -> bool { return net_socket_close(self.handle); }
}

object TcpListener {
    public let handle: int;

    public init(handle: int) { self.handle = handle; }

    public fn valid() -> bool { return self.handle > 0; }
    public fn port() -> int { return net_socket_local_port(self.handle); }
    public fn accept(timeout_ms: int) -> TcpStream {
        return TcpStream(net_tcp_accept(self.handle, timeout_ms));
    }
    public fn close() -> bool { return net_socket_close(self.handle); }
}

fn tcp_connect(host: str, port: int, timeout_ms: int) -> TcpStream {
    return TcpStream(net_tcp_connect(host, port, timeout_ms));
}

fn tcp_listen(host: str, port: int, backlog: int) -> TcpListener {
    return TcpListener(net_tcp_listen(host, port, backlog));
}

async fn tcp_connect_async(host: str, port: int, timeout_ms: int) -> TcpStream {
    return tcp_connect(host, port, timeout_ms);
}

async fn tcp_accept_async(listener_handle: int, timeout_ms: int) -> TcpStream {
    return TcpStream(net_tcp_accept(listener_handle, timeout_ms));
}
