import std.net.tcp
import std.net.http
import std.data.base64

# RFC 6455 WebSocket support over ws://. The frame layer handles masking,
# fragmentation, ping/pong, close frames, binary payloads, and timeout-aware
# reads. wss:// deliberately waits for a reviewed raw TLS stream binding rather
# than implementing TLS in PunPun.

object WebSocketMessage {
    public let opcode: int;
    public let data: bytes;
    public let error: str;

    public init(opcode: int, data: bytes, error: str) {
        self.opcode = opcode;
        self.data = data;
        self.error = error;
    }

    public fn valid() -> bool { return len(self.error) == 0; }
    public fn is_text() -> bool { return self.opcode == 1; }
    public fn is_binary() -> bool { return self.opcode == 2; }
    public fn text() -> str { return bytes_to_text(self.data); }
}

object WebSocket {
    public let handle: int;
    public let client_side: bool;
    public let error: str;

    public init(handle: int, client_side: bool, error: str) {
        self.handle = handle;
        self.client_side = client_side;
        self.error = error;
    }

    public fn valid() -> bool { return self.handle > 0 && len(self.error) == 0; }
    public fn send_text(value: str, timeout_ms: int) -> bool {
        return _ws_send_frame(self.handle, self.client_side, 1, bytes_from_text(value), timeout_ms);
    }
    public fn send_binary(value: bytes, timeout_ms: int) -> bool {
        return _ws_send_frame(self.handle, self.client_side, 2, value, timeout_ms);
    }
    public fn ping(value: bytes, timeout_ms: int) -> bool {
        return _ws_send_frame(self.handle, self.client_side, 9, value, timeout_ms);
    }
    public fn recv(timeout_ms: int) -> WebSocketMessage {
        return _ws_recv_message(self.handle, self.client_side, timeout_ms);
    }
    public fn close(timeout_ms: int) -> bool {
        if self.handle <= 0 { return true; }
        let sent = _ws_send_frame(self.handle, self.client_side, 8, bytes(), timeout_ms);
        net_socket_close(self.handle);
        return sent;
    }
}

object WsFrame {
    public let fin: bool;
    public let opcode: int;
    public let data: bytes;
    public let error: str;

    public init(fin: bool, opcode: int, data: bytes, error: str) {
        self.fin = fin;
        self.opcode = opcode;
        self.data = data;
        self.error = error;
    }
}

fn _ws_rotl32(value: int, amount: int) -> int {
    let masked = value & 4294967295;
    return ((masked << amount) | (masked >> (32 - amount))) & 4294967295;
}

fn _ws_sha1(input: bytes) -> bytes {
    let original_length = bytes_len(input);
    let mut message = bytes_slice(input, 0, original_length);
    bytes_push(message, 128);
    while (bytes_len(message) % 64) != 56 { bytes_push(message, 0); }
    let bit_length = original_length * 8;
    for shift in [56, 48, 40, 32, 24, 16, 8, 0] {
        bytes_push(message, (bit_length >> shift) & 255);
    }

    let mut h0 = 1732584193;
    let mut h1 = 4023233417;
    let mut h2 = 2562383102;
    let mut h3 = 271733878;
    let mut h4 = 3285377520;

    let mut block = 0;
    while block < bytes_len(message) {
        let words = list<int>();
        for i in 0..16 {
            let at = block + i * 4;
            let word = (bytes_at(message, at) << 24) | (bytes_at(message, at + 1) << 16) | (bytes_at(message, at + 2) << 8) | bytes_at(message, at + 3);
            list_push(words, word & 4294967295);
        }
        for i in 16..80 {
            let mixed = list_at(words, i - 3) ^ list_at(words, i - 8) ^ list_at(words, i - 14) ^ list_at(words, i - 16);
            list_push(words, _ws_rotl32(mixed, 1));
        }

        let mut a = h0;
        let mut b = h1;
        let mut c = h2;
        let mut d = h3;
        let mut e = h4;
        for i in 0..80 {
            let mut f = 0;
            let mut k = 0;
            if i < 20 {
                f = (b & c) | ((~b) & d);
                k = 1518500249;
            } else if i < 40 {
                f = b ^ c ^ d;
                k = 1859775393;
            } else if i < 60 {
                f = (b & c) | (b & d) | (c & d);
                k = 2400959708;
            } else {
                f = b ^ c ^ d;
                k = 3395469782;
            }
            f = f & 4294967295;
            let temp = (_ws_rotl32(a, 5) + f + e + k + list_at(words, i)) & 4294967295;
            e = d;
            d = c;
            c = _ws_rotl32(b, 30);
            b = a;
            a = temp;
        }
        h0 = (h0 + a) & 4294967295;
        h1 = (h1 + b) & 4294967295;
        h2 = (h2 + c) & 4294967295;
        h3 = (h3 + d) & 4294967295;
        h4 = (h4 + e) & 4294967295;
        block = block + 64;
    }

    let result = bytes();
    for word in [h0, h1, h2, h3, h4] {
        bytes_push(result, (word >> 24) & 255);
        bytes_push(result, (word >> 16) & 255);
        bytes_push(result, (word >> 8) & 255);
        bytes_push(result, word & 255);
    }
    return result;
}

fn _ws_accept_value(key: str) -> str {
    return base64_encode(_ws_sha1(bytes_from_text(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")));
}

fn _ws_recv_exact(handle: int, count: int, timeout_ms: int) -> bytes {
    let mut out = bytes();
    let started = clock_ms();
    while bytes_len(out) < count {
        let remaining = timeout_ms - (clock_ms() - started);
        if remaining <= 0 { return bytes(); }
        let chunk = net_socket_recv(handle, count - bytes_len(out), remaining);
        if bytes_len(chunk) == 0 { return bytes(); }
        out = bytes_concat(out, chunk);
    }
    return out;
}

fn _ws_send_frame(handle: int, client_side: bool, opcode: int, payload: bytes,
                  timeout_ms: int) -> bool {
    if handle <= 0 || opcode < 0 || opcode > 15 { return false; }
    let length = bytes_len(payload);
    if length > 16777216 { return false; }
    if opcode >= 8 && length > 125 { return false; }

    let frame = bytes();
    bytes_push(frame, 128 | opcode);
    let mut mask_flag = 0;
    if client_side { mask_flag = 128; }
    if length < 126 {
        bytes_push(frame, mask_flag | length);
    } else if length <= 65535 {
        bytes_push(frame, mask_flag | 126);
        bytes_push(frame, (length >> 8) & 255);
        bytes_push(frame, length & 255);
    } else {
        bytes_push(frame, mask_flag | 127);
        for shift in [56, 48, 40, 32, 24, 16, 8, 0] {
            bytes_push(frame, (length >> shift) & 255);
        }
    }

    let mut mask = bytes();
    if client_side {
        mask = random_bytes(4);
        for i in 0..4 { bytes_push(frame, bytes_at(mask, i)); }
    }
    for i in 0..length {
        let mut value = bytes_at(payload, i);
        if client_side { value = value ^ bytes_at(mask, i % 4); }
        bytes_push(frame, value);
    }
    return net_socket_send(handle, frame, timeout_ms) == bytes_len(frame);
}

fn _ws_read_frame(handle: int, client_side: bool, timeout_ms: int) -> WsFrame {
    let first = _ws_recv_exact(handle, 2, timeout_ms);
    if bytes_len(first) != 2 { return WsFrame(false, 0, bytes(), "WebSocket frame header timed out or closed"); }
    let b0 = bytes_at(first, 0);
    let b1 = bytes_at(first, 1);
    let fin = (b0 & 128) != 0;
    let rsv = b0 & 112;
    let opcode = b0 & 15;
    let masked = (b1 & 128) != 0;
    let mut length = b1 & 127;
    if rsv != 0 { return WsFrame(fin, opcode, bytes(), "WebSocket extensions are not negotiated"); }
    if client_side && masked { return WsFrame(fin, opcode, bytes(), "a WebSocket server frame must not be masked"); }
    if not client_side && not masked { return WsFrame(fin, opcode, bytes(), "a WebSocket client frame must be masked"); }

    if length == 126 {
        let extended = _ws_recv_exact(handle, 2, timeout_ms);
        if bytes_len(extended) != 2 { return WsFrame(fin, opcode, bytes(), "incomplete WebSocket length"); }
        length = (bytes_at(extended, 0) << 8) | bytes_at(extended, 1);
    } else if length == 127 {
        let extended = _ws_recv_exact(handle, 8, timeout_ms);
        if bytes_len(extended) != 8 { return WsFrame(fin, opcode, bytes(), "incomplete WebSocket length"); }
        length = 0;
        for i in 0..8 {
            if length > 36028797018963967 { return WsFrame(fin, opcode, bytes(), "WebSocket frame is too large"); }
            length = (length << 8) | bytes_at(extended, i);
        }
    }
    if length > 16777216 { return WsFrame(fin, opcode, bytes(), "WebSocket frame exceeds 16 MiB"); }
    if opcode >= 8 && (not fin || length > 125) {
        return WsFrame(fin, opcode, bytes(), "invalid WebSocket control frame");
    }

    let mut mask = bytes();
    if masked {
        mask = _ws_recv_exact(handle, 4, timeout_ms);
        if bytes_len(mask) != 4 { return WsFrame(fin, opcode, bytes(), "incomplete WebSocket mask"); }
    }
    let encoded = _ws_recv_exact(handle, length, timeout_ms);
    if bytes_len(encoded) != length { return WsFrame(fin, opcode, bytes(), "incomplete WebSocket payload"); }
    if not masked { return WsFrame(fin, opcode, encoded, ""); }
    let decoded = bytes();
    for i in 0..length { bytes_push(decoded, bytes_at(encoded, i) ^ bytes_at(mask, i % 4)); }
    return WsFrame(fin, opcode, decoded, "");
}

fn _ws_recv_message(handle: int, client_side: bool, timeout_ms: int) -> WebSocketMessage {
    let started = clock_ms();
    let mut message = bytes();
    let mut message_opcode = 0;
    let mut fragmented = false;
    while true {
        let remaining = timeout_ms - (clock_ms() - started);
        if remaining <= 0 { return WebSocketMessage(0, bytes(), "WebSocket receive timed out"); }
        let frame = _ws_read_frame(handle, client_side, remaining);
        if len(frame.error) > 0 { return WebSocketMessage(0, bytes(), frame.error); }

        if frame.opcode == 8 {
            net_socket_close(handle);
            return WebSocketMessage(8, frame.data, "");
        }
        if frame.opcode == 9 {
            if not _ws_send_frame(handle, client_side, 10, frame.data, remaining) {
                return WebSocketMessage(0, bytes(), "failed to send WebSocket pong");
            }
            continue;
        }
        if frame.opcode == 10 { continue; }

        if frame.opcode == 1 || frame.opcode == 2 {
            if fragmented { return WebSocketMessage(0, bytes(), "new WebSocket message before continuation"); }
            message_opcode = frame.opcode;
            message = frame.data;
            fragmented = not frame.fin;
            if frame.fin { return WebSocketMessage(message_opcode, message, ""); }
            continue;
        }
        if frame.opcode == 0 {
            if not fragmented { return WebSocketMessage(0, bytes(), "unexpected WebSocket continuation frame"); }
            message = bytes_concat(message, frame.data);
            if bytes_len(message) > 16777216 { return WebSocketMessage(0, bytes(), "WebSocket message exceeds 16 MiB"); }
            if frame.fin { return WebSocketMessage(message_opcode, message, ""); }
            continue;
        }
        return WebSocketMessage(0, bytes(), "unsupported WebSocket opcode");
    }
    return WebSocketMessage(0, bytes(), "WebSocket receive stopped unexpectedly");
}

fn websocket_accept(stream_handle: int, request: HttpRequest, timeout_ms: int) -> WebSocket {
    if stream_handle <= 0 || not request.valid() {
        return WebSocket(-1, false, "invalid WebSocket upgrade request");
    }
    if to_upper(request.method) != "GET" {
        return WebSocket(-1, false, "WebSocket upgrade must use GET");
    }
    if not contains(to_lower(request.header("upgrade")), "websocket") || not contains(to_lower(request.header("connection")), "upgrade") {
        return WebSocket(-1, false, "missing WebSocket Upgrade headers");
    }
    let key = trim(request.header("sec-websocket-key"));
    if len(key) == 0 || trim(request.header("sec-websocket-version")) != "13" {
        return WebSocket(-1, false, "unsupported WebSocket handshake");
    }
    let response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + _ws_accept_value(key) + "\r\n\r\n";
    if net_socket_send(stream_handle, bytes_from_text(response), timeout_ms) != len(response) {
        return WebSocket(-1, false, net_error());
    }
    return WebSocket(stream_handle, false, "");
}

fn websocket_connect(url: str, timeout_ms: int) -> WebSocket {
    if starts_with(url, "wss://") {
        return WebSocket(-1, true, "wss:// needs the future raw TLS stream API; use ws:// or HTTPS APIs today");
    }
    if not starts_with(url, "ws://") {
        return WebSocket(-1, true, "WebSocket URL must begin with ws://");
    }
    let http_url = "http://" + slice(url, 5, len(url));
    let parsed = _http_parse_url(http_url);
    if not parsed.valid() { return WebSocket(-1, true, parsed.error); }
    let stream = tcp_connect(parsed.host, parsed.port, timeout_ms);
    if not stream.valid() { return WebSocket(-1, true, net_error()); }

    let key = base64_encode(random_bytes(16));
    let mut host = parsed.host;
    if parsed.port != 80 { host = host + ":" + text(parsed.port); }
    let request = "GET " + parsed.path + " HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    if stream.send_text(request, timeout_ms) != len(request) {
        let problem = net_error();
        stream.close();
        return WebSocket(-1, true, problem);
    }

    let started = clock_ms();
    let mut response = bytes();
    let mut header_end = -1;
    // Read the upgrade header delimiter exactly. A server is allowed to send
    // its first WebSocket frame in the same TCP packet as the 101 response; a
    // larger recv here would consume those frame bytes before WebSocket.recv()
    // can see them. Handshakes are capped at 16 KiB, so byte-at-a-time reads
    // keep the state machine correct without affecting steady-state traffic.
    while header_end < 0 && bytes_len(response) <= 16384 {
        let remaining = timeout_ms - (clock_ms() - started);
        if remaining <= 0 { stream.close(); return WebSocket(-1, true, "WebSocket handshake timed out"); }
        let chunk = stream.recv(1, remaining);
        if bytes_len(chunk) == 0 { stream.close(); return WebSocket(-1, true, "WebSocket handshake closed early"); }
        response = bytes_concat(response, chunk);
        header_end = _http_find_header_end(response);
    }
    if header_end < 0 { stream.close(); return WebSocket(-1, true, "WebSocket handshake headers are too large"); }
    let header_text = bytes_to_text(response);
    let line_end = index_of(header_text, "\r\n", 0);
    if line_end < 0 || not contains(slice(header_text, 0, line_end), " 101 ") {
        stream.close();
        return WebSocket(-1, true, "WebSocket server rejected the upgrade");
    }
    let headers = _http_parse_headers_text(header_text);
    let accept = map_get_or(headers, "sec-websocket-accept", "");
    if accept != _ws_accept_value(key) {
        stream.close();
        return WebSocket(-1, true, "invalid Sec-WebSocket-Accept value");
    }
    return WebSocket(stream.handle, true, "");
}

async fn websocket_connect_async(url: str, timeout_ms: int) -> WebSocket {
    return websocket_connect(url, timeout_ms);
}
