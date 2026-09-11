# requests

Compatibility package for the PunPun 1.0 requests API, now backed by the
verified PunPun 1.4 HTTPS runtime.

```sh
ppx add requests
```

`requests_get`, `requests_post`, `requests_put`, `requests_patch`,
`requests_delete`, `requests_head`, and `requests_request` return
`HttpResponse` with `status`, `body`, `error`, `ok()`, and `text()`.
Task-returning async variants preserve the same response type.

Only `https://` URLs are accepted. TLS peer/hostname verification is mandatory,
redirects remain HTTPS-only, and response bodies are capped at 64 MiB.
