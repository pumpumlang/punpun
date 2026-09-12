import std.net.tcp
import std.net.udp
import std.net.dns

async fn echo_once(listener_handle: int) -> int {
    let stream = await tcp_accept_async(listener_handle, 5000);
    if not stream.valid() { return -1; }
    let input = stream.recv(1024, 5000);
    let sent = stream.send(input, 5000);
    stream.close();
    return sent;
}

launch {
    say(net_available());
    say(list_size(dns_resolve("localhost")) > 0);

    let listener = tcp_listen("127.0.0.1", 0, 8);
    say(listener.valid());
    let no_client = net_tcp_accept(listener.handle, 0);
    say(no_client < 0 && net_timed_out());

    let echo = echo_once(listener.handle);
    let client = await tcp_connect_async("127.0.0.1", listener.port(), 5000);
    say(client.valid());
    say(client.peer_port() == listener.port());
    say(client.send_text("tcp-ping", 5000));
    say(client.recv_text(1024, 5000));
    say(await echo);
    client.close();
    listener.close();

    let receiver = udp_bind("127.0.0.1", 0);
    let sender = udp_bind("127.0.0.1", 0);
    say(receiver.valid() && sender.valid());
    say(sender.send_text_to("127.0.0.1", receiver.port(), "udp-ping", 5000));
    let packet = receiver.recv_from(1024, 5000);
    say(packet.text());
    say(packet.host == "127.0.0.1");
    say(packet.port == sender.port());
    sender.close();
    receiver.close();
}
