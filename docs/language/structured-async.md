# Structured async and cancellation

PunPun 0.8 adds runtime-owned task groups on top of the native `async fn` and
`await` model. A group does not change the result type of a task. It gives the
caller one explicit lifetime boundary for waiting, cancellation, and cleanup.

```punpun doctest-run
async fn work(value: i64) -> i64 {
    sleep_ms(5);
    return value;
}

launch {
    let group = task_group();
    let left = work(20);
    let right = work(22);
    task_group_add(group, left);
    task_group_add(group, right);
    task_group_wait(group);
    assert(await left + await right == 42, "task group result");
    task_group_close(group);
}
```

Cancellation is cooperative. `sleep_ms` is a cancellation safe point and
returns early after a cancellation request; CPU-bound workers should check
`cancelled()` at sensible loop boundaries.

```punpun doctest-run
async fn cancellable() -> i64 {
    sleep_ms(5000);
    if cancelled() { return 1; }
    return 0;
}

launch {
    let group = task_group();
    let task = cancellable();
    task_group_add(group, task);
    task_group_cancel(group);
    assert(task_group_wait_for(group, 500), "cancelled task should finish");
    assert(await task == 1, "worker observed cancellation");
    task_group_close(group);
}
```

`task_group_wait_for(group, milliseconds)` is a bounded wait; it does not
forcibly kill native code. A timeout returns `no`, leaving the group valid so
the caller can cancel or wait later.
