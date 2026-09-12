import std.net.tcp
import std.net.https

# Structured HTTP/1.1 on top of std.net.tcp. Plain HTTP uses PunPun sockets;
# HTTPS uses the verified libcurl runtime but returns the same response shape.
# Request/response bodies are bytes so binary payloads survive unchanged.

object HttpResponse {
    public let status: int;
    public let reason: str;
    public let headers: Map<str>;
    public let body: bytes;
    public let error: str;

    public init(status: int, reason: str, headers: Map<str>, body: bytes, error: str) {
        self.status = status;
        self.reason = reason;
        self.headers = headers;
        self.body = body;
        self.error = error;
    }

    public fn ok() -> bool {
        return self.status >= 200 && self.status < 300 && len(self.error) == 0;
    }
    public fn text() -> str { return bytes_to_text(self.body); }
    public fn header(name: str) -> str {
        return map_get_or(self.headers, to_lower(name), "");
    }
}

object HttpRequest {
    public let method: str;
    public let path: str;
    public let version: str;
    public let headers: Map<str>;
    public let body: bytes;
    public let peer_host: str;
    public let peer_port: int;
    public let error: str;

    public init(method: str, path: str, version: str, headers: Map<str>, body: bytes,
                peer_host: str, peer_port: int, error: str) {
        self.method = method;
        self.path = path;
        self.version = version;
        self.headers = headers;
        self.body = body;
        self.peer_host = peer_host;
        self.peer_port = peer_port;
        self.error = error;
    }

    public fn valid() -> bool { return len(self.error) == 0; }
    public fn text() -> str { return bytes_to_text(self.body); }
    public fn header(name: str) -> str {
        return map_get_or(self.headers, to_lower(name), "");
    }
}

object HttpUrl {
    public let secure: bool;
    public let host: str;
    public let port: int;
    public let path: str;
    public let error: str;

    public init(secure: bool, host: str, port: int, path: str, error: str) {
        self.secure = secure;
        self.host = host;
        self.port = port;
        self.path = path;
        self.error = error;
    }

    public fn valid() -> bool { return len(self.error) == 0; }
}

object HttpClient {
    public let timeout_ms: int;
    public let headers: Map<str>;
    public let follow_redirects: bool;

    public init(timeout_ms: int, follow_redirects: bool) {
        self.timeout_ms = timeout_ms;
        self.follow_redirects = follow_redirects;
        self.headers = map<str>();
    }

    public fn set_header(name: str, value: str) -> void {
        http_header_set(self.headers, name, value);
    }

    public fn request(method: str, url: str, body: bytes) -> HttpResponse {
        return http_request(method, url, self.headers, body, self.timeout_ms,
                            self.follow_redirects);
    }

    public fn get(url: str) -> HttpResponse { return self.request("GET", url, bytes()); }
    public fn delete(url: str) -> HttpResponse { return self.request("DELETE", url, bytes()); }
    public fn post(url: str, body: bytes) -> HttpResponse { return self.request("POST", url, body); }
    public fn put(url: str, body: bytes) -> HttpResponse { return self.request("PUT", url, body); }
    public fn patch(url: str, body: bytes) -> HttpResponse { return self.request("PATCH", url, body); }
}

struct HttpChunkHead { size: int, offset: int, valid: bool }

fn http_headers() -> Map<str> { return map<str>(); }

fn http_header_set(headers: Map<str>, name: str, value: str) -> void {
    let clean_name = to_lower(trim(name));
    if len(clean_name) == 0 || contains(clean_name, "\r") || contains(clean_name, "\n") || contains(clean_name, ":") || contains(value, "\r") || contains(value, "\n") {
        return;
    }
    map_put(headers, clean_name, trim(value));
}

fn http_header_get(headers: Map<str>, name: str) -> str {
    return map_get_or(headers, to_lower(name), "");
}

fn _http_parse_decimal(value: str) -> int {
    if len(value) == 0 { return -1; }
    let mut out = 0;
    for i in 0..len(value) {
        let c = char_at(value, i);
        if c < 48 || c > 57 { return -1; }
        if out > 576460752303423487 { return -1; }
        out = out * 10 + (c - 48);
    }
    return out;
}

fn _http_parse_url(url: str) -> HttpUrl {
    let mut secure = false;
    let mut offset = 0;
    let mut port = 80;
    if starts_with(url, "https://") {
        secure = true;
        offset = 8;
        port = 443;
    } else if starts_with(url, "http://") {
        offset = 7;
    } else {
        return HttpUrl(false, "", 0, "", "URL must begin with http:// or https://");
    }

    let slash = index_of(url, "/", offset);
    let mut authority = "";
    let mut path = "/";
    if slash < 0 {
        authority = slice(url, offset, len(url));
    } else {
        authority = slice(url, offset, slash);
        path = slice(url, slash, len(url));
    }
    if len(authority) == 0 { return HttpUrl(secure, "", 0, path, "URL has no host"); }

    let mut host = authority;
    if starts_with(authority, "[") {
        let close = index_of(authority, "]", 1);
        if close < 0 { return HttpUrl(secure, "", 0, path, "invalid bracketed IPv6 host"); }
        host = slice(authority, 1, close);
        if close + 1 < len(authority) {
            if char_at(authority, close + 1) != 58 {
                return HttpUrl(secure, "", 0, path, "invalid IPv6 authority");
            }
            let parsed = _http_parse_decimal(slice(authority, close + 2, len(authority)));
            if parsed < 1 || parsed > 65535 {
                return HttpUrl(secure, "", 0, path, "invalid URL port");
            }
            port = parsed;
        }
    } else {
        let colon = last_index_of(authority, ":");
        if colon >= 0 {
            host = slice(authority, 0, colon);
            let parsed = _http_parse_decimal(slice(authority, colon + 1, len(authority)));
            if parsed < 1 || parsed > 65535 {
                return HttpUrl(secure, "", 0, path, "invalid URL port");
            }
            port = parsed;
        }
    }
    if len(host) == 0 { return HttpUrl(secure, "", 0, path, "URL has no host"); }
    return HttpUrl(secure, host, port, path, "");
}

fn _http_header_lines(headers: Map<str>, skip_transport: bool) -> str {
    let mut out = "";
    let keys = map_keys(headers);
    for key in keys {
        let lower = to_lower(key);
        if skip_transport && (lower == "host" || lower == "connection" || lower == "content-length" || lower == "transfer-encoding") {
            continue;
        }
        let value = map_get(headers, key);
        if len(key) == 0 || contains(key, "\r") || contains(key, "\n") || contains(key, ":") || contains(value, "\r") || contains(value, "\n") {
            continue;
        }
        out = out + key + ": " + value + "\r\n";
    }
    return out;
}

fn _https_header_lines(headers: Map<str>) -> str {
    let mut out = "";
    let keys = map_keys(headers);
    for key in keys {
        let value = map_get(headers, key);
        if len(key) == 0 || contains(key, "\r") || contains(key, "\n") || contains(key, ":") || contains(value, "\r") || contains(value, "\n") {
            continue;
        }
        out = out + key + ": " + value + "\n";
    }
    return out;
}

fn _http_parse_headers_text(source: str) -> Map<str> {
    let headers = map<str>();
    let lines = split(source, "\r\n");
    let mut first = true;
    for line in lines {
        if first { first = false; continue; }
        let colon = index_of(line, ":", 0);
        if colon <= 0 { continue; }
        let name = to_lower(trim(slice(line, 0, colon)));
        let value = trim(slice(line, colon + 1, len(line)));
        if len(name) > 0 { map_put(headers, name, value); }
    }
    return headers;
}

fn _http_find_header_end(data: bytes) -> int {
    let length = bytes_len(data);
    if length < 4 { return -1; }
    for i in 0..(length - 3) {
        if bytes_at(data, i) == 13 && bytes_at(data, i + 1) == 10 && bytes_at(data, i + 2) == 13 && bytes_at(data, i + 3) == 10 {
            return i + 4;
        }
    }
    return -1;
}

fn _http_hex_digit(value: int) -> int {
    if value >= 48 && value <= 57 { return value - 48; }
    if value >= 65 && value <= 70 { return value - 65 + 10; }
    if value >= 97 && value <= 102 { return value - 97 + 10; }
    return -1;
}

fn _http_chunk_head(data: bytes, start: int) -> HttpChunkHead {
    let length = bytes_len(data);
    let mut pos = start;
    let mut size = 0;
    let mut digits = 0;
    let mut extension = false;
    while pos + 1 < length {
        let c = bytes_at(data, pos);
        if c == 13 && bytes_at(data, pos + 1) == 10 {
            return HttpChunkHead(size, pos + 2, digits > 0);
        }
        if c == 59 {
            extension = true;
        } else if not extension {
            let digit = _http_hex_digit(c);
            if digit < 0 || digits >= 8 { return HttpChunkHead(0, start, false); }
            size = size * 16 + digit;
            digits = digits + 1;
        }
        pos = pos + 1;
    }
    return HttpChunkHead(0, start, false);
}

fn _http_decode_chunked(data: bytes) -> bytes {
    let mut out = bytes();
    let mut pos = 0;
    while pos < bytes_len(data) {
        let head = _http_chunk_head(data, pos);
        if not head.valid { return bytes(); }
        if head.size == 0 { return out; }
        let finish = head.offset + head.size;
        if finish + 2 > bytes_len(data) { return bytes(); }
        if bytes_at(data, finish) != 13 || bytes_at(data, finish + 1) != 10 {
            return bytes();
        }
        out = bytes_concat(out, bytes_slice(data, head.offset, finish));
        pos = finish + 2;
    }
    return bytes();
}

fn _http_reason(status: int) -> str {
    if status == 200 { return "OK"; }
    if status == 201 { return "Created"; }
    if status == 204 { return "No Content"; }
    if status == 301 { return "Moved Permanently"; }
    if status == 302 { return "Found"; }
    if status == 304 { return "Not Modified"; }
    if status == 400 { return "Bad Request"; }
    if status == 401 { return "Unauthorized"; }
    if status == 403 { return "Forbidden"; }
    if status == 404 { return "Not Found"; }
    if status == 405 { return "Method Not Allowed"; }
    if status == 408 { return "Request Timeout"; }
    if status == 413 { return "Payload Too Large"; }
    if status == 429 { return "Too Many Requests"; }
    if status == 500 { return "Internal Server Error"; }
    if status == 502 { return "Bad Gateway"; }
    if status == 503 { return "Service Unavailable"; }
    return "Status";
}

fn _http_response_from_wire(wire: bytes, transport_error: str) -> HttpResponse {
    if len(transport_error) > 0 {
        return HttpResponse(0, "", map<str>(), bytes(), transport_error);
    }
    let header_end = _http_find_header_end(wire);
    if header_end < 0 {
        return HttpResponse(0, "", map<str>(), bytes(), "incomplete HTTP response headers");
    }
    let header_text = bytes_to_text(bytes_slice(wire, 0, header_end));
    let line_end = index_of(header_text, "\r\n", 0);
    if line_end < 0 { return HttpResponse(0, "", map<str>(), bytes(), "invalid HTTP status line"); }
    let status_line = slice(header_text, 0, line_end);
    let first_space = index_of(status_line, " ", 0);
    if first_space < 0 { return HttpResponse(0, "", map<str>(), bytes(), "invalid HTTP status line"); }
    let second_space = index_of(status_line, " ", first_space + 1);
    let mut status_text = "";
    let mut reason = "";
    if second_space < 0 {
        status_text = slice(status_line, first_space + 1, len(status_line));
    } else {
        status_text = slice(status_line, first_space + 1, second_space);
        reason = slice(status_line, second_space + 1, len(status_line));
    }
    let status = _http_parse_decimal(status_text);
    if status < 100 || status > 999 {
        return HttpResponse(0, "", map<str>(), bytes(), "invalid HTTP status code");
    }
    let headers = _http_parse_headers_text(header_text);
    let mut body = bytes_slice(wire, header_end, bytes_len(wire));
    let transfer = to_lower(map_get_or(headers, "transfer-encoding", ""));
    if contains(transfer, "chunked") {
        let decoded = _http_decode_chunked(body);
        if bytes_len(body) > 0 && bytes_len(decoded) == 0 {
            return HttpResponse(status, reason, headers, bytes(), "invalid chunked HTTP body");
        }
        body = decoded;
    }
    return HttpResponse(status, reason, headers, body, "");
}

fn http_request(method: str, url: str, headers: Map<str>, body: bytes,
                timeout_ms: int, follow_redirects: bool) -> HttpResponse {
    if timeout_ms <= 0 {
        return HttpResponse(0, "", map<str>(), bytes(), "HTTP timeout must be positive");
    }
    if len(method) == 0 || contains(method, " ") || contains(method, "\r") || contains(method, "\n") {
        return HttpResponse(0, "", map<str>(), bytes(), "invalid HTTP method");
    }
    let parsed = _http_parse_url(url);
    if not parsed.valid() {
        return HttpResponse(0, "", map<str>(), bytes(), parsed.error);
    }

    if parsed.secure {
        let response_body = https_request_bytes(to_upper(method), url, body,
                                                _https_header_lines(headers), timeout_ms,
                                                follow_redirects);
        let raw_headers = https_headers_raw();
        let parsed_headers = _http_parse_headers_text(raw_headers);
        let status = https_status();
        let mut reason = "";
        let line_end = index_of(raw_headers, "\r\n", 0);
        if line_end > 0 {
            let line = slice(raw_headers, 0, line_end);
            let a = index_of(line, " ", 0);
            let mut b = -1;
            if a >= 0 { b = index_of(line, " ", a + 1); }
            if b >= 0 { reason = slice(line, b + 1, len(line)); }
        }
        return HttpResponse(status, reason, parsed_headers, response_body, https_error());
    }

    let stream = tcp_connect(parsed.host, parsed.port, timeout_ms);
    if not stream.valid() {
        return HttpResponse(0, "", map<str>(), bytes(), net_error());
    }

    let default_port = parsed.port == 80;
    let mut host_header = parsed.host;
    if not default_port { host_header = host_header + ":" + text(parsed.port); }
    let mut request_head = to_upper(method) + " " + parsed.path + " HTTP/1.1\r\n";
    request_head = request_head + "Host: " + host_header + "\r\n";
    request_head = request_head + "User-Agent: PunPun-Net/1.4\r\n";
    request_head = request_head + "Accept: */*\r\n";
    request_head = request_head + "Accept-Encoding: identity\r\n";
    request_head = request_head + _http_header_lines(headers, true);
    request_head = request_head + "Content-Length: " + text(bytes_len(body)) + "\r\n";
    request_head = request_head + "Connection: close\r\n\r\n";

    let started = clock_ms();
    let sent_head = stream.send_text(request_head, timeout_ms);
    if sent_head != len(request_head) {
        let problem = net_error();
        stream.close();
        return HttpResponse(0, "", map<str>(), bytes(), problem);
    }
    if bytes_len(body) > 0 {
        let remaining = timeout_ms - (clock_ms() - started);
        if remaining <= 0 || stream.send(body, remaining) != bytes_len(body) {
            let problem = net_error();
            stream.close();
            return HttpResponse(0, "", map<str>(), bytes(), problem);
        }
    }

    let mut wire = bytes();
    let mut problem = "";
    while bytes_len(wire) <= 67108864 {
        let remaining = timeout_ms - (clock_ms() - started);
        if remaining <= 0 { problem = "HTTP response timed out"; break; }
        let chunk = stream.recv(65536, remaining);
        if bytes_len(chunk) > 0 {
            wire = bytes_concat(wire, chunk);
            continue;
        }
        if net_eof() { break; }
        if net_timed_out() { problem = "HTTP response timed out"; break; }
        if len(net_error()) > 0 { problem = net_error(); break; }
    }
    if bytes_len(wire) > 67108864 { problem = "HTTP response exceeded 64 MiB"; }
    stream.close();
    return _http_response_from_wire(wire, problem);
}

fn http_get(url: str) -> HttpResponse {
    return http_request("GET", url, map<str>(), bytes(), 30000, true);
}

fn http_delete(url: str) -> HttpResponse {
    return http_request("DELETE", url, map<str>(), bytes(), 30000, true);
}

fn http_post_text(url: str, body: str, content_type: str) -> HttpResponse {
    let headers = map<str>();
    http_header_set(headers, "content-type", content_type);
    return http_request("POST", url, headers, bytes_from_text(body), 30000, true);
}

fn http_response_text(status: int, body: str, content_type: str) -> HttpResponse {
    let headers = map<str>();
    http_header_set(headers, "content-type", content_type);
    return HttpResponse(status, _http_reason(status), headers, bytes_from_text(body), "");
}

fn http_read_request(stream_handle: int, max_body_bytes: int, timeout_ms: int) -> HttpRequest {
    let stream = TcpStream(stream_handle);
    let empty_headers = map<str>();
    if not stream.valid() {
        return HttpRequest("", "", "", empty_headers, bytes(), "", 0, "invalid TCP stream");
    }
    if max_body_bytes < 0 || max_body_bytes > 67108864 {
        return HttpRequest("", "", "", empty_headers, bytes(), stream.peer_host(),
                           stream.peer_port(), "HTTP body limit must be 0..64 MiB");
    }

    let started = clock_ms();
    let mut wire = bytes();
    let mut header_end = -1;
    while header_end < 0 && bytes_len(wire) <= 1048576 {
        let remaining = timeout_ms - (clock_ms() - started);
        if remaining <= 0 {
            return HttpRequest("", "", "", empty_headers, bytes(), stream.peer_host(),
                               stream.peer_port(), "HTTP request headers timed out");
        }
        let chunk = stream.recv(65536, remaining);
        if bytes_len(chunk) > 0 { wire = bytes_concat(wire, chunk); header_end = _http_find_header_end(wire); continue; }
        if net_eof() { break; }
        if net_timed_out() { continue; }
        if len(net_error()) > 0 {
            return HttpRequest("", "", "", empty_headers, bytes(), stream.peer_host(),
                               stream.peer_port(), net_error());
        }
    }
    if header_end < 0 {
        return HttpRequest("", "", "", empty_headers, bytes(), stream.peer_host(),
                           stream.peer_port(), "incomplete or oversized HTTP request headers");
    }

    let header_text = bytes_to_text(bytes_slice(wire, 0, header_end));
    let line_end = index_of(header_text, "\r\n", 0);
    if line_end < 0 {
        return HttpRequest("", "", "", empty_headers, bytes(), stream.peer_host(),
                           stream.peer_port(), "invalid HTTP request line");
    }
    let request_line = slice(header_text, 0, line_end);
    let first = index_of(request_line, " ", 0);
    let mut second = -1;
    if first >= 0 { second = index_of(request_line, " ", first + 1); }
    if first <= 0 || second <= first + 1 {
        return HttpRequest("", "", "", empty_headers, bytes(), stream.peer_host(),
                           stream.peer_port(), "invalid HTTP request line");
    }
    let method = slice(request_line, 0, first);
    let path = slice(request_line, first + 1, second);
    let version = slice(request_line, second + 1, len(request_line));
    if version != "HTTP/1.1" && version != "HTTP/1.0" {
        return HttpRequest(method, path, version, empty_headers, bytes(), stream.peer_host(),
                           stream.peer_port(), "unsupported HTTP version");
    }
    let headers = _http_parse_headers_text(header_text);
    let content_length_text = map_get_or(headers, "content-length", "0");
    let content_length = _http_parse_decimal(content_length_text);
    if content_length < 0 || content_length > max_body_bytes {
        return HttpRequest(method, path, version, headers, bytes(), stream.peer_host(),
                           stream.peer_port(), "invalid or oversized Content-Length");
    }

    let wanted = header_end + content_length;
    while bytes_len(wire) < wanted {
        let remaining = timeout_ms - (clock_ms() - started);
        if remaining <= 0 {
            return HttpRequest(method, path, version, headers, bytes(), stream.peer_host(),
                               stream.peer_port(), "HTTP request body timed out");
        }
        let need = wanted - bytes_len(wire);
        let mut amount = 65536;
        if need < amount { amount = need; }
        let chunk = stream.recv(amount, remaining);
        if bytes_len(chunk) > 0 { wire = bytes_concat(wire, chunk); continue; }
        if net_eof() { break; }
        if len(net_error()) > 0 {
            return HttpRequest(method, path, version, headers, bytes(), stream.peer_host(),
                               stream.peer_port(), net_error());
        }
    }
    if bytes_len(wire) < wanted {
        return HttpRequest(method, path, version, headers, bytes(), stream.peer_host(),
                           stream.peer_port(), "incomplete HTTP request body");
    }
    let body = bytes_slice(wire, header_end, wanted);
    return HttpRequest(method, path, version, headers, body, stream.peer_host(),
                       stream.peer_port(), "");
}

fn http_write_response(stream_handle: int, response: HttpResponse, timeout_ms: int) -> bool {
    let stream = TcpStream(stream_handle);
    if not stream.valid() { return false; }
    let mut reason = response.reason;
    if len(reason) == 0 { reason = _http_reason(response.status); }
    if contains(reason, "\r") || contains(reason, "\n") { reason = "Status"; }
    let mut head = "HTTP/1.1 " + text(response.status) + " " + reason + "\r\n";
    head = head + "Server: PunPun-Net/1.4\r\n";
    head = head + _http_header_lines(response.headers, true);
    head = head + "Content-Length: " + text(bytes_len(response.body)) + "\r\n";
    head = head + "Connection: close\r\n\r\n";
    let started = clock_ms();
    if stream.send_text(head, timeout_ms) != len(head) { return false; }
    if bytes_len(response.body) > 0 {
        let remaining = timeout_ms - (clock_ms() - started);
        if remaining <= 0 || stream.send(response.body, remaining) != bytes_len(response.body) {
            return false;
        }
    }
    stream.shutdown_write();
    return true;
}

fn http_serve_once(listener_handle: int, handler: fn(HttpRequest) -> HttpResponse,
                   timeout_ms: int, max_body_bytes: int) -> bool {
    let stream_handle = net_tcp_accept(listener_handle, timeout_ms);
    if stream_handle <= 0 { return false; }
    let request = http_read_request(stream_handle, max_body_bytes, timeout_ms);
    let response = handler(request);
    let written = http_write_response(stream_handle, response, timeout_ms);
    net_socket_close(stream_handle);
    return written;
}

async fn http_request_async(method: str, url: str, headers: Map<str>, body: bytes,
                            timeout_ms: int, follow_redirects: bool) -> HttpResponse {
    return http_request(method, url, headers, body, timeout_ms, follow_redirects);
}

async fn http_get_async(url: str) -> HttpResponse {
    return http_get(url);
}

async fn http_post_text_async(url: str, body: str, content_type: str) -> HttpResponse {
    return http_post_text(url, body, content_type);
}
