# PunPun 0.8 structured concurrency contract

## Task groups

A task group is a runtime-owned handle containing zero or more PunPun tasks. `task_group_add(group, task)` associates an existing task with the group without transferring the runtime's ownership of the individual task object.

Supported operations are creation, add, wait, timed wait, pending-count, done query, cancellation and close. Adding the same task twice is idempotent. Invalid group handles are rejected by the runtime.

## Cancellation

Cancellation is cooperative. Group cancellation requests cancellation on every member; it does not asynchronously tear down a worker at an arbitrary instruction. `sleep_ms` is a cancellation safe point and returns early when the current task has been cancelled. Worker loops should query `cancelled()` at natural safe boundaries.

`task_group_wait` joins group members. `task_group_close` removes the group and releases group membership state after joining members; individual task storage remains runtime-owned by the task subsystem.

## Ownership boundary

Safe PunPun ownership rules still apply across async task creation. Task groups organize lifetime/wait/cancel behavior; they do not weaken cross-task move/borrow checks.
