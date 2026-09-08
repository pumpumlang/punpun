# PunPun 0.7 development language reference

PunPun is a statically typed, ahead-of-time compiled language. The normal Linux
x86-64 build uses the direct native backend; a portable C backend supports
additional toolchains. This page describes the supported 0.5 syntax. The older
`launch: ... done` dialect is accepted only by the migration tool.

## Program structure

```punpun
bring std::io;

fn twice(value: i64 = 21) -> i64 {
    return value * 2;
}

launch {
    say(twice());
    say(twice(value: 10));
}
```

Files use `.pp`, comments start with `//`, blocks use braces, and statements end
with semicolons. A complete executable has one `launch` block. Imported module
paths use `::`.

## Bindings and core types

```punpun
let name: text = "PunPun";
let mut count: i64 = 1;
count = count + 1;
```

`let` is immutable and `let mut` permits reassignment. Current built-in types
include `i64`, `f64`, `bool`, `text`, `nums`, `void`, safe references `&T` and
`&mut T`, raw pointers `*T`, concrete objects/structs, and typed async tasks.
There are no implicit numeric or text conversions.

Function, constructor, and method calls support positional arguments, named
arguments, and literal default values. Positional arguments must precede named
arguments. A parameter without a default cannot follow one with a default.

## Functions and control flow

```punpun
fn classify(value: i64) -> text {
    if value > 10 {
        return "large";
    } else {
        return "small";
    }
}

launch {
    let mut i = 0;
    while i < 3 {
        say(classify(i));
        i += 1;
    }
}
```

Conditions are `bool`. Integer arithmetic is checked. Calls and operands are
evaluated left-to-right; `and` and `or` short-circuit. Non-void functions must
conservatively return a value on every path.

## Objects, structs, and contracts

```punpun
contract Named {
    fn name() -> text;
}

object User meets Named {
    private let label: text;

    public init(label: text) {
        self.label = label;
    }

    public fn name() -> text {
        return self.label;
    }
}

launch {
    let user = User(label: "Ada");
    say(user.name());
}
```

Objects have identity-oriented runtime storage. Structs are value-oriented.
Fields and methods can be public or private. Contracts currently provide
compile-time conformance for concrete types; contract-typed values and mature
dynamic dispatch are not yet implemented.

## References, raw pointers, move, and drop

```punpun
fn bump(value: &mut i64) {
    *value += 1;
}

launch {
    let mut value = 41;
    bump(&mut value);

    unsafe {
        let pointer: *i64 = &raw value;
        *pointer += 1;
    }

    let mut first = User("Ada");
    let second = move(first);
    drop(second);
    first = User("Grace");
}
```

Safe references participate in the compiler's borrow and escape checks. Raw
pointer creation, dereference, and arithmetic require `unsafe`. `move` transfers
an owned object or `nums` value, and subsequent use is rejected until a mutable
binding is reinitialized. `drop` deterministically releases a supported owned
value. Full lexical drop insertion and production lifetime analysis remain
future work.

## Async tasks

```punpun
async fn work(value: i64) -> i64 {
    if cancelled() {
        return 0;
    }
    return value;
}

launch {
    let task = work(42);
    say(await task);
}
```

Async calls create native tasks. `await` is valid in `async fn` and `launch`.
`cancel(task)`, `task_done(task)`, and `cancelled()` provide cooperative
cancellation and completion inspection. See [language/async.md](language/async.md).

## Native injection and FFI

`extern native fn` declarations bridge PunPun calls to explicitly injected C,
C++, Rust, or assembly blocks. Injection is compiled only by `build`/`run`, is
content-addressed and cached, and is never evaluated by `check` or the language
server. See [injection/README.md](injection/README.md).

## Diagnostics and current boundaries

The frontend uses source spans and stable diagnostic codes. `pp explain CODE`
shows extended guidance. `pp emit-hir`, `pp emit-ir`, and `ppc emit-machine-ir` expose typed HIR, verified MIR, and target-aware Machine IR for compiler work.

The distribution also includes a PunPun-written fixed-point compiler for the
documented bootstrap subset. Run `make selfhost` or use
`pp selfhost input.pp output.c`; see [`selfhost/README.md`](../selfhost/README.md).

The 0.6 language foundation executes inferred and explicit generic calls,
deterministically monomorphizes generic types and methods, checks algebraic
enum patterns for reachability/exhaustiveness, and supports `Option<T>`,
`Result<T,E>`, postfix `?`, ownership/borrow checks and lexical destruction for
supported owning values. PunPun 0.7 development now includes a verified Machine
IR foundation with explicit ABI and allocation metadata. Detailed direct-x86
instruction selection is still being migrated from typed AST helpers into
Machine IR during Step 7. Contract-typed dynamic dispatch and closures/first-
class functions remain later work. The authoritative implementation matrix is
in [`COMPLETION_REPORT.md`](../COMPLETION_REPORT.md).

Normative 0.6 rules and implementation status are in [`spec/0.6/`](../spec/0.6/).

## 0.6 ownership and slices

Identity objects are move-only. `move(value)` explicitly transfers a binding and `drop(value)` ends it immediately. The compiler also tracks maybe-moved values across branch joins and rejects later use. Normal scope exits destroy supported live move-only object values once.

`nums` intentionally retains its 0.5 shared-handle behavior in 0.6. Use `view(values, start, end)` to create a checked `Slice<int>`; `slice_len(view)` and `slice_get(view, index)` access it. The compiler prevents checked mutation/move of the source while a named slice borrow is live.

## Optional LLVM backend

The default supported Linux x86-64 path is PunPun's direct backend. `--cc-backend` chooses portable C lowering. `--llvm-backend` chooses an optional Clang/LLVM path after the same parser, semantic analysis, ownership checks, typed HIR, verified MIR and Machine IR authority chain. `emit-llvm` writes inspectable LLVM IR. The LLVM compatibility path remains native-host only for the currently supported development host.
