import std.net.websocket

async fn websocket_echo_server(listener_handle: int) -> bool {
    let client = net_tcp_accept(listener_handle, 5000);
    if client <= 0 { return false; }
    let request = http_read_request(client, 0, 5000);
    let socket = websocket_accept(client, request, 5000);
    if not socket.valid() { net_socket_close(client); return false; }
    let message = socket.recv(5000);
    if not message.valid() || not message.is_text() { socket.close(1000); return false; }

    # A ping may appear between fragments of a data message. Send an unmasked
    # server ping followed by a fragmented text response; the client must pong
    # automatically and still reassemble the two data frames as one message.
    let ping_frame = bytes();
    bytes_push(ping_frame, 137); # FIN + ping
    bytes_push(ping_frame, 1);
    bytes_push(ping_frame, 112); # 'p'
    if net_socket_send(socket.handle, ping_frame, 5000) != bytes_len(ping_frame) { return false; }

    let mut first = bytes();
    bytes_push(first, 1); # text, FIN clear
    bytes_push(first, 5);
    first = bytes_concat(first, bytes_from_text("echo:"));
    if net_socket_send(socket.handle, first, 5000) != bytes_len(first) { return false; }

    let tail_text = message.text();
    let mut second = bytes();
    bytes_push(second, 128); # FIN + continuation
    bytes_push(second, len(tail_text));
    second = bytes_concat(second, bytes_from_text(tail_text));
    return net_socket_send(socket.handle, second, 5000) == bytes_len(second);
}

launch {
    # RFC 6455 section 1.3 handshake vector. This pins SHA-1 + Base64 behavior.
    say(_ws_accept_value("dGhlIHNhbXBsZSBub25jZQ=="));

    let listener = tcp_listen("127.0.0.1", 0, 8);
    let server = websocket_echo_server(listener.handle);
    let socket = websocket_connect("ws://127.0.0.1:" + text(listener.port()) + "/chat", 5000);
    say(socket.valid());
    say(socket.send_text("hello", 5000));
    let response = socket.recv(5000);
    say(response.valid());
    say(response.text());
    say(await server);
    socket.close(1000);
    listener.close();

    let secure = websocket_connect("wss://example.com/chat", 1000);
    say(not secure.valid() && contains(secure.error, "TLS"));
}
