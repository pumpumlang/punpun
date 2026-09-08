# HTTP Requests

The first-party `requests` beta package uses the system libcurl dynamically, keeping ordinary PunPun programs free of HTTP startup/link cost.

```punpun
bring requests;

launch {
    let response = requests_get("https://example.com");
    say(response.status);
    say(response.text());
}
```

GET, POST, PUT, PATCH, DELETE and HEAD are implemented with headers, timeouts and redirect controls. The response exposes status/body/error. Streaming, connection pooling and async-native sockets are not yet declared complete.
