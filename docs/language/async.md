# Async tasks and structured concurrency

PunPun's `async fn` is native AOT concurrency. An async call starts a runtime
task and returns a typed task handle; `await` joins it and produces its result.

```pp
async fn delayed(value: i64, delay_ms: i64) -> i64 {
    sleep_ms(delay_ms);
    return value;
}

launch {
    let a = delayed(20, 100);
    let b = delayed(22, 100);
    say(await a + await b);
}
```

## Structured task groups

Use `task_group()` when several tasks belong to one operation:

```pp
let group = task_group();
let a = delayed(20, 10);
let b = delayed(22, 10);
task_group_add(group, a);
task_group_add(group, b);
task_group_wait(group);
task_group_close(group);
```

Available operations are `task_group_add`, `task_group_cancel`,
`task_group_wait`, `task_group_wait_for`, `task_group_done`,
`task_group_pending`, and `task_group_close`.

Groups do not consume task results. A task can still be awaited normally after
the group has waited for it.

## Cancellation

Cancellation is cooperative rather than forced thread termination. `cancelled()`
returns whether the current worker has received a request. `sleep_ms` is a safe
point and returns early when cancellation is requested. Long CPU loops should
check `cancelled()` themselves.

## Task-boundary ownership

Async parameters must be independently owned/copyable values. Borrowed
references, raw pointers, `nums` handles, and object identities are rejected at
task boundaries until a future `Send`-style proof system exists. This keeps a
worker from retaining aliases to a caller's mutable stack state.

## I/O

`std::async` exposes `read_text_async` and `write_text_async`. The first-party
`requests` package exposes `requests_get_async`, `requests_post_async`, and the
other HTTP verbs. These currently run blocking host APIs inside native PunPun
workers. They are real concurrent tasks, but they are not advertised as an
epoll/io_uring/IOCP coroutine engine.
