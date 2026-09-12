# ppc architecture

How a `.pp` file becomes a running program, and why each stage is shaped the
way it is.

```
source ─► lexer ─► parser ─► checker ─► MIR builder ─► optimizer ─► backend
          tokens    AST       HIR         CFG            CFG         artifact
```

Three intermediate representations, each earning its place: the AST keeps the
program as written so diagnostics can point at real syntax, HIR is typed and
desugared so backends never do name lookup, and MIR is a control-flow graph so
the optimizer and code generators work on explicit branches.

---

## Source management and diagnostics

`SourceManager` owns every byte the compiler reads. Positions are byte offsets
rather than line/column pairs, because the lexer produces millions of them and
only the handful that reach a diagnostic ever need converting. That conversion
is a binary search over a per-file table of line starts.

`DiagnosticEngine` collects rather than throws, so one run reports many
independent problems. It also supports `mark()` and `rewind()`, which the parser
uses for speculative parsing: `f<int>(x)` and `a < b` are indistinguishable
until a matching `>` is found, so the parser tries one, and on failure rolls
back both the token position and anything it reported.

Codes are grouped by phase (`E00xx` lexical, `E04xx` types, `E08xx` ownership),
so a number alone locates the problem. `docs/diagnostics.md` documents all 65
and is what `ppc explain` reads at runtime — the compiler and the manual cannot
drift, because there is only one copy.

## Lexer

One structural rule matters: a newline emits a synthetic `;` unless the lexer is
inside `(` or `[`. Braces do *not* suppress it, because the modern grammar still
ends statements at end of line. That is what lets `let x = 1` work without a
semicolon while `foo(a,\n b)` keeps working across lines.

Both dialects' keywords are live at once. `yes`/`no` are normalized to
`true`/`false` in the lexer, so no later phase knows they existed.

## Parser

Recursive descent with Pratt-style binary expressions, producing one AST for
both dialects. Where they differ only in spelling — `fn`/`craft`, `if`/`when`,
`{}` versus `:`+`done` — the difference is absorbed here.

The interesting case is the migration dialect's `otherwise`, which *terminates*
the consequent rather than closing it: the whole `when`/`otherwise` shares one
trailing `done`. Blocks therefore record which style opened them, and the
conditional parser handles the two shapes separately.

Errors recover at declaration and statement boundaries so one bad line does not
cascade.

## Checker

Three passes, so declarations can reference each other in any order:

1. **collect** — register every type, enum, contract, and function name
2. **resolve** — fill in field, variant, parameter, and result types
3. **check** — walk bodies, emitting HIR and diagnostics

### Generics

Monomorphized on demand. A call computes its type arguments (explicit first,
then unified from argument types, then from the expected result type), and
`specialize()` returns a cached instantiation or queues a new one. Specialization
keys include the mangled type arguments, so `Box<int>` and `Box<str>` are
distinct declarations with independently resolved fields.

Only what the entry point can reach is instantiated. A recursion limit catches
generics that never reach a concrete type.

### Argument probing

Overload resolution needs an argument's type before the parameter type is known,
so arguments are checked twice: once to probe, once for real with the parameter
as context. The probe runs inside a `SpeculativeScope` that restores move state,
borrow counts, introduced locals, and diagnostics. Without it, a `move(x)`
argument would be marked moved during the probe and then report a spurious
use-after-move during the real check.

### Exhaustiveness

Counting which variant tags appear is not enough: `Item(Some(v))` and
`Item(None)` name the same variant and only together cover it. The checker runs
Maranget's usefulness algorithm over a pattern matrix instead, specializing on
each constructor and recursing into payload columns. It reports a concrete
uncovered value — `Nested::Item(Option::None)`, not "some variant missing".

The same routine answers reachability: an arm is unreachable exactly when the
arms above it already cover everything.

Guarded arms are excluded from the matrix, because a guard can fail.

### Ownership

Each binding tracks initialized / moved / maybe-moved. Branches are checked
against a snapshot and merged: moved on both sides stays moved, moved on one
becomes maybe-moved. Whole-value assignment revives a binding — so an assignment
*target* is deliberately not treated as a use.

Borrows are lexical. A `view` of a list increments the source's borrow count and
records where; the count is released when the borrowing binding leaves scope.
Mutating a borrowed list is rejected while the count is nonzero.

### A note on storage

`TypeContext` and the specialization table hand out references that are read
across calls which can create *more* types and specializations. Both therefore
box their entries, so a vector reallocation cannot invalidate a live reference.
This was found by ASan, not by inspection, and the fix is structural rather than
per-call-site on purpose.

## HIR

Typed and fully resolved: names point at local slots or specialization indices,
operators know their operand types, enum construction carries a variant index.
`?` is desugared here into a match that returns the failing variant, so no later
phase knows it exists. Constructors with an `init` become a block that binds a
temporary, calls the initializer on it, and yields it.

## MIR

A basic-block CFG with virtual registers. Every register is assigned once by
construction; locals stay as memory slots rather than being promoted, because
the C backend hands them to the host compiler as ordinary C locals and lets its
SSA construction do that work.

Structured control flow becomes explicit edges here — `if`, `while`, `for`,
`match`, and short-circuiting `and`/`or` all turn into branches. `match` lowers
to a chain of tag tests, each followed by payload bindings and then any guard,
in that order, because a guard may read the bindings.

### Optimizer

Constant folding, copy propagation, branch simplification, dead code
elimination, unreachable block removal, and block merging. `-O1` runs one sweep;
`-O2` iterates to a fixed point.

Two details worth knowing. Folding refuses any operation that would overflow or
divide by zero, since folding it would erase a runtime trap. And folding clears
the operand fields of the instruction it rewrites — dead code elimination counts
uses by scanning those fields, so a folded instruction that still named its old
inputs would keep them artificially alive.

## Backends

All three consume the same MIR through one `Backend` interface.

**C.** One self-contained translation unit. Aggregates become real C structs, so
value semantics come for free. This is the only backend with full language
coverage; async becomes a context struct plus a trampoline handed to
`pp_task_spawn`.

**Native x86-64.** System V assembly, AT&T syntax. Locals and MIR values keep
8-byte spill homes, but scalar MIR values are assigned across `%rbx` and
`%r12`–`%r15` with a CFG-aware linear-scan allocator; those callee-saved
registers survive runtime/helper calls and loop backedges are accounted for by
block liveness. Calls use the full scalar System V convention, including stack
arguments after the six GP or eight SSE argument registers. Async calls lower
to generated native context wrappers/trampolines around `pp_task_spawn`, and
`await` calls the same runtime accessors as the C backend. Checked arithmetic is
inline (`jo` to a shared per-function trap stub) rather than a call.

**Bytecode.** A register-based VM, not a stack machine — the mapping from MIR is
nearly one-to-one and there is no push/pop traffic. Slots are untagged, because
MIR is fully typed and the compiler already picked the right opcode.

### Aggregates outside C

The native and bytecode backends box every aggregate into a heap block. That is
right for an `object`, which has identity, and unobservable for an enum, which
cannot be mutated in place. It is wrong for a `struct`, which the language
specifies as a value — so both backends insert an explicit deep copy wherever a
value struct reaches a new home: a local binding, a field, a call argument, a
constructor argument, or a read of a nested field.

This was found by differential testing, not by reading the spec: `showcase.pp`
asserts that a nested record copies by value, and the bytecode backend failed
that assert while C passed. The regression is pinned by
`tests/cases/value_semantics.pp`.

## Runtime

`runtime/ppcrt.c` is the canonical PunPun ABI 1 runtime. HTTPS and GUI are
separate runtime translation units and expose the same entry points to native
code and the VM.

Everything handed to PunPun code is tracked and released in one sweep at exit;
callers never free. Checked arithmetic lives here too, so overflow behaves
identically no matter which backend produced the program. The VM links the same
runtime the native backends do, which is what makes cross-backend output
identical rather than merely similar.

## Driver

Resolves imports (both `::` and `.` separate segments; a sibling file shadows a
standard library module), runs the pipeline, and invokes the host toolchain.

The runtime object is compiled once and cached under a key derived from the
runtime's contents and the chosen compiler, so changing either rebuilds and
nothing else does.

## Testing

`tests/run_tests.py` runs every case against every backend. A case is a `.pp`
file plus either `.out` (expected stdout) or `.err` (diagnostic codes that must
be reported, and no others). A `.skip` file names backends that cannot run the
case — and those are still compiled, to confirm they refuse cleanly with a
diagnostic rather than crashing or producing wrong output.

Running every case on every backend is the point: it is what caught the value
semantics divergence, and it is the only thing that keeps three code generators
honest.
