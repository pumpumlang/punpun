# Async, task groups, and cancellation

PunPun 0.9 keeps the native `async fn` / `await` model and adds structured task
groups. Calling an async function returns a typed native task immediately.

```pp
async fn fetch_later(value: i64) -> i64 {
    sleep_ms(50);
    return value;
}

launch {
    let group = task_group();
    let left = fetch_later(20);
    let right = fetch_later(22);
    task_group_add(group, left);
    task_group_add(group, right);
    task_group_wait(group);
    say(await left + await right);
    task_group_close(group);
}
```

A group provides one explicit boundary for waiting, cancellation, completion
queries, and cleanup. `task_group_wait_for(group, milliseconds)` returns `no` on
timeout while leaving the group valid.

Cancellation is cooperative. `cancel(task)` and `task_group_cancel(group)` set a
request flag. `cancelled()` observes that flag from inside the current worker,
and `sleep_ms` is a cancellation safe point that returns early after a request.
CPU-bound workers should check `cancelled()` at sensible loop boundaries.

The runtime still uses native worker threads rather than an always-on event
loop. Ordinary non-async programs create no executor. First-party filesystem and
HTTP helpers provide task-returning wrappers so structured concurrency can be
used today without pretending thread-backed I/O is a kernel-native async socket
engine.
