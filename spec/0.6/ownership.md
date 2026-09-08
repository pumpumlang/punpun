# PunPun 0.6 ownership and destruction

Status: implemented for the 0.6 beta vertical slice.

## Value categories

Every value is classified as `Copy`, shared-handle, borrowed, or move-only.

- Primitive numbers, booleans, strings, raw pointers and immutable safe references are `Copy`/borrow-like values for 0.6 purposes.
- A value struct is `Copy` only when every field is `Copy` or a legacy shared handle.
- Identity `object` values are move-only and receive deterministic lexical destruction.
- `nums` remains the 0.5-compatible shared list handle in 0.6: ordinary assignment and parameter passing alias the same list. This is intentional source/behavior compatibility, not a claim that all future collections will be shared handles.
- `move(value)` explicitly transfers a move-only value and may also explicitly end the source binding for a `nums` handle.

Future owned collection types may use move-by-default semantics without silently changing the legacy `nums` contract.

## Move-state analysis

The compiler tracks each local as initialized, moved or maybe moved across control-flow joins. Reading, borrowing, moving or dropping a moved/maybe-moved binding is rejected. A mutable local may become initialized again through whole-value assignment.

Partial moves from fields and indexed elements are rejected in 0.6. This keeps destruction deterministic until field-level move paths are specified.

## Borrowing

- `&T` is a shared, read-only borrow.
- `&mut T` is an exclusive mutable borrow.
- Any number of shared borrows or one mutable borrow may be live, never both.
- A borrow cannot outlive its owner, escape a shorter scope, cross owner destruction or survive a conflicting mutation.
- Async tasks cannot capture a non-static borrow in 0.6.
- Raw pointers do not extend a lifetime and do not weaken safe-reference checks.

The checked `Slice<int>` view is non-owning. `view(nums, start, end)` ties the slice lifetime to its source list. While a named slice is live, moving or mutating that source through checked operations is rejected. `slice_len` and `slice_get` perform bounds-safe access.

## Deterministic `Drop` and lexical destruction

Move-only object locals that remain initialized are destroyed exactly once in reverse declaration order at normal scope exit, including normal `return`, `break`, and `continue` paths. Explicit `drop(value)` ends the binding lifetime immediately. Reinitializing a moved mutable binding creates a new lifetime.

PunPun 0.6 aborts on panic and does not promise stack unwinding, so lexical destruction is guaranteed on normal control flow, not after an aborting panic.

Custom user-defined destructor hooks are not part of the 0.6 beta grammar. The compiler/runtime currently perform structural/runtime destruction for supported owning values. User-defined destructor methods are later work and must not be inferred from this specification.

## Allocation policy

Ownership is independent of allocator choice. Ordinary identity objects use the runtime allocator. Allocator parameters, arenas, placement APIs, owned generic collections, and reference-counted library types are later work.
