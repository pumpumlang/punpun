# PunPun 0.6 ownership and destruction

## Value categories

Every type is either `Copy` or move-only.

- Primitive numbers, booleans and immutable safe references are `Copy`.
- A struct is `Copy` only when every field is `Copy` and the type has no destructor.
- Owned objects, buffers, strings and values with destructors are move-only.
- Raw pointers are `Copy` but dereferencing them is unsafe.

Assignment and by-value parameter passing copy `Copy` values and move move-only values. `move(value)` remains accepted as explicit documentation but is not required when the context is unambiguously consuming.

## Move-state analysis

The compiler tracks each local as initialized, moved or maybe moved across control-flow joins. Reading, borrowing, moving or dropping a moved/maybe-moved value is rejected. A mutable local may become initialized again through whole-value assignment.

Partial moves from fields and indexed elements are rejected in 0.6. This keeps destruction deterministic until field-level move paths are specified.

## Borrowing

- `&T` is a shared, read-only borrow.
- `&mut T` is an exclusive mutable borrow.
- Any number of shared borrows or one mutable borrow may be live, never both.
- A borrow cannot outlive its owner, escape a shorter scope, cross owner destruction or survive a conflicting mutation.
- Async tasks cannot capture a non-static borrow in 0.6.
- Raw pointers do not extend a lifetime and do not weaken safe-reference checks.

The compiler may shorten a borrow to its last use. Diagnostics must identify the borrow creation, conflicting operation and later use.

## Deterministic `Drop`

Move-only locals that remain initialized are dropped exactly once in reverse declaration order at every normal scope exit, including `return`, `break` and `continue`. Fields are dropped in reverse declaration order after the type's own `drop` method runs.

`drop(value)` ends the value's lifetime immediately and changes its state to moved. Destructors cannot be overloaded by return type, cannot be async, and cannot move `self` after field destruction begins.

PunPun 0.6 aborts on panic and does not promise stack unwinding. Consequently, lexical destructors are guaranteed on normal control flow, not after an aborting panic. This limitation must remain visible in documentation.

## Allocation policy

Ownership is independent of allocator choice. Ordinary owned values use the runtime allocator; allocator parameters, arenas and placement APIs are later work. Reference counting is provided by explicit library types rather than being the default object model.
