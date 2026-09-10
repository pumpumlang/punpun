import std.net.https

launch {
    let body = https_get("https://example.com");
    if https_ok() {
        say("status:");
        say(https_status());
        say(body);
    } else {
        say(https_error());
    }
}
