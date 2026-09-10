# https

First-party verified HTTPS support for PunPun 1.3. The package uses the same
runtime-backed API as `std.net.https`, so C, native x86-64, and bytecode builds
share one implementation.

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

TLS peer and hostname verification are mandatory. Redirects are restricted to
HTTPS, response bodies are capped at 64 MiB, and the system libcurl runtime is
loaded dynamically.
