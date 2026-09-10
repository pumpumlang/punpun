# Verified HTTPS

PunPun 1.3 provides HTTPS in the runtime and through `std.net.https`. Programs
that do not call it pay no HTTP startup or link-time dependency cost because
libcurl is loaded dynamically.

```punpun
import std.net.https

launch {
    let body = https_get("https://example.com");
    if https_ok() {
        say(body);
    } else {
        say(https_error());
    }
}
```

The primitive `https_request(method, url, body, headers, timeout_ms, follow)`
supports custom headers and request methods. The most recent response status and
error are available through `https_status()` and `https_error()`.

Only HTTPS URLs are accepted. Peer and hostname verification are mandatory,
redirects remain HTTPS-only, timeouts are bounded, and bodies are capped at
64 MiB.

The `requests` package preserves the older `HttpResponse` API. Async helpers
run blocking libcurl work inside PunPun worker tasks; this is task concurrency,
not a nonblocking socket reactor.
