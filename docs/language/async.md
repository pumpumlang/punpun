# Async tasks in PunPun 0.5.0-beta

PunPun has a native `async fn` / `await` foundation in 0.5.0-beta. Async calls create native tasks and return immediately; `await` joins the task and yields its typed result.

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

The two calls above may execute concurrently. This is AOT native code, not an interpreter and not source-to-source async lowering through another language.

## Where `await` is valid

`await` is accepted inside an `async fn` and in the root `launch` block. Using it in an ordinary function is `E1503`.

## Results

Current task results support scalar values, strings/pointer-like values, and `void`. The portable C backend and direct Linux x86-64 backend use the same language semantics.

## Cross-task safety in the beta

PunPun deliberately rejects parameters that would cross a task boundary without a proven ownership model, including borrowed references, raw pointers, `nums` handles, and object identities. Such calls produce `E1502`. Copyable scalar/string parameters are supported.

This restriction is conservative. It prevents a worker from retaining an unsafe reference to a caller stack frame while the ownership/`Send` analysis is still under development.

## Runtime model

On Linux, the current beta task runtime uses native pthread workers. On Windows
the runtime source has a Win32 thread implementation. Argument contexts are
copied before the worker starts and task handles are cleaned up by the runtime.

Tasks expose cooperative cancellation and completion queries:

```pp
let task = delayed(42, 100);
cancel(task);
say(task_done(task));
```

`cancel` requests cancellation; it does not forcibly terminate native code.
Running task code may query `cancelled()` at safe points and return promptly.

Programs that never call an async function do not create async tasks or initialize a task executor.

## Current boundary

0.5.0-beta does **not** yet claim a production event-loop runtime. Forced
preemption, nonblocking socket integration, structured concurrency, async file
I/O, and compiler-generated coroutine/state-machine lowering remain ongoing
work. The existing implementation is a real native concurrent task foundation
and is covered by direct-x86, portable-C, and LSP tests.
