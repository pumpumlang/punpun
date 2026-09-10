// From spec/0.8 structured-async: cancellation is cooperative, and sleep_ms is
// a safe point, so a long sleep aborts promptly instead of running to term.
async fn cancellable() -> i64 {
    sleep_ms(5000);
    if cancelled() { return 1; }
    return 0;
}

launch {
    let started = clock_ms();
    let group = task_group();
    let task = cancellable();
    task_group_add(group, task);
    task_group_cancel(group);
    assert(task_group_wait_for(group, 500), "cancelled task should finish");
    assert(await task == 1, "worker observed cancellation");
    task_group_close(group);
    say(clock_ms() - started < 2000);
}
