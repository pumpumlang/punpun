async fn work(value: i64, delay: i64) -> i64 {
    sleep_ms(delay);
    if cancelled() { return -1; }
    return value;
}

launch {
    let group = task_group();
    let a = work(20, 5);
    let b = work(22, 5);
    task_group_add(group, a);
    task_group_add(group, b);
    assert(task_group_pending(group) >= 0, "pending count");
    assert(task_group_wait_for(group, 1000), "group timeout");
    say(await a + await b);
    assert(task_group_done(group), "group done");
    task_group_close(group);
}
