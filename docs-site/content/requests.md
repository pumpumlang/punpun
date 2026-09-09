# HTTP requests

The first-party `requests` package loads the system libcurl runtime dynamically,
so programs that never use HTTP pay no HTTP startup/link cost.

```punpun
bring requests;

launch {
    let response = requests_get("https://example.com");
    say(response.status);
    say(response.text());
}
```

GET, POST, PUT, PATCH, DELETE and HEAD support headers, timeouts, redirect
controls, status/body/error reporting, and structured error paths.

## Async wrappers

Every major request helper also has a task-returning async form:

```punpun
bring requests;

launch {
    let group = task_group();
    let request = requests_get_async("https://example.com");
    task_group_add(group, request);
    task_group_wait(group);
    let response = await request;
    say(response.status);
    task_group_close(group);
}
```

The async wrappers execute libcurl work on PunPun native workers. This composes
with cancellation/task groups without claiming that libcurl easy-mode calls are
a kernel-native event loop.
