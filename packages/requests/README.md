# requests

First-party PunPun HTTP package backed by the system libcurl runtime when
available. Install from a source checkout/SDK with:

```sh
ppx add requests
```

Synchronous helpers include `requests_get`, `requests_post`, `requests_put`,
`requests_patch`, `requests_delete`, `requests_head`, and the configurable
`requests_request`.

PunPun 0.8+ also exposes task-returning async wrappers such as
`requests_get_async` and `requests_request_async`. They run through the native
PunPun task runtime and compose with `task_group_*` structured-concurrency
builtins.
