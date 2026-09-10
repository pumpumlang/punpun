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
    say(task_group_pending(group));
    say(task_group_done(group));
    task_group_close(group);
    say("structured async ok");
}
