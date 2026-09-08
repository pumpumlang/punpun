# Async and await

PunPun 0.6 development retains the native task-based `async fn` and `await` foundation.

```pp
async fn fetch_later(value: i64) -> i64 {
    sleep_ms(50);
    return value;
}

launch {
    let left = fetch_later(20);
    let right = fetch_later(22);
    say(await left + await right);
}
```

Calling an async function schedules a native task and returns a typed task handle. `await` is legal in another async function or in `launch`, which acts as the root executor.

The beta uses native worker threads rather than an interpreter. It deliberately rejects borrowed references, raw pointers and other not-yet-proven-shareable values across task boundaries.

## What is still beta

The current implementation is a concurrent task foundation with cooperative cancellation through `cancel`, `task_done`, and `cancelled`. Structured task groups, event-loop/network readiness integration and coroutine state-machine lowering remain future 0.x work.
