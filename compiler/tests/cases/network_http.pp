import std.net.http

async fn serve_handler(listener_handle: int, handler: fn(HttpRequest) -> HttpResponse) -> bool {
    return http_serve_once(listener_handle, handler, 5000, 1048576);
}

async fn serve_chunked(listener_handle: int) -> bool {
    let client = net_tcp_accept(listener_handle, 5000);
    if client <= 0 { return false; }
    let request = net_socket_recv(client, 8192, 5000);
    if bytes_len(request) == 0 { net_socket_close(client); return false; }
    let wire = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nX-Chunked: yes\r\nConnection: close\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
    let sent = net_socket_send(client, bytes_from_text(wire), 5000);
    net_socket_close(client);
    return sent == len(wire);
}

launch {
    let prefix = "captured:";
    let handler: fn(HttpRequest) -> HttpResponse = fn(request: HttpRequest) -> HttpResponse {
        if not request.valid() { return http_response_text(400, request.error, "text/plain"); }
        let response = http_response_text(201, prefix + request.method + ":" + request.path + ":" + request.text(), "text/plain");
        http_header_set(response.headers, "x-punpun", "network");
        return response;
    };

    let listener = tcp_listen("127.0.0.1", 0, 8);
    let port = listener.port();
    let server = serve_handler(listener.handle, handler);
    let url = "http://127.0.0.1:" + text(port) + "/submit";
    let response = http_post_text(url, "body", "text/plain");
    say(response.status);
    say(response.reason);
    say(response.header("x-punpun"));
    say(response.text());
    say(await server);
    listener.close();

    let chunk_listener = tcp_listen("127.0.0.1", 0, 8);
    let chunk_port = chunk_listener.port();
    let chunk_server = serve_chunked(chunk_listener.handle);
    let chunk_response = http_get("http://127.0.0.1:" + text(chunk_port) + "/chunks");
    say(chunk_response.ok());
    say(chunk_response.header("x-chunked"));
    say(chunk_response.text());
    say(await chunk_server);
    chunk_listener.close();

    let bad = http_get("ftp://example.com/");
    say(bad.status);
    say(len(bad.error) > 0);
}
