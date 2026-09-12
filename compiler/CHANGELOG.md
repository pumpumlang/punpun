# Changelog

## Unreleased — native ABI and allocator pass

- Direct x86-64 now uses CFG-aware linear-scan allocation for scalar MIR values
  in callee-saved registers, while keeping spill homes for deterministic debug
  and materialization paths.
- System V calls now spill overflow scalar arguments to the stack instead of
  rejecting more than six GP or eight SSE arguments.
- Native async functions now generate runtime task contexts/trampolines and
  support spawn, await, cancellation, task groups, and floating-point results.
- Direct synchronous native calls consume `analyze_parameters` and omit deep
  copies for read-only value-struct parameters.
- Added structural codegen regressions to pin stack arguments, allocator output,
  and native task wrappers.

## Unreleased — 47 stdlib modules, 89 new builtins, and an ABI bug

### Added — 25 more stdlib modules

`collections/` gained list_ops, heap, and grid. `text/` gained casing, distance,
and wrap. `data/` gained checksum, csv, query, ini, binary, and uuid. New
`math_ext/` (integers, bits, vector, constants, floats, statistics, shuffling)
and `system_ext/` (cli, console, log, timer, paths, files).

47 modules total, every one exercised by the regression suite on all three
backends.

### Fixed — boolean ABI in the native backend

A C function returning `bool` sets only `%al`; System V leaves the upper 56 bits
of `%rax` unspecified. The native backend stored the whole register, so
`if !map_has(...)` tested garbage and took the wrong branch. Booleans returned
from any call are now zero-extended.

This produced **silently wrong control flow**, not a crash, and only when the
garbage happened to be nonzero — which is why earlier tests using the same
builtins passed. Found by a stdlib module, not by inspection. Pinned by
`tests/cases/bool_abi.pp`.

### Fixed — INT64_MIN emission

The C backend emitted `INT64_C(-9223372036854775808)`, which C parses as unary
minus applied to a value one past INT64_MAX. Now written as a subtraction so
every intermediate stays in range.

### Design notes worth knowing

- **CLI parsing requires `--name=value`.** `--name value` is ambiguous without a
  schema: in `--verbose input.txt` there is no way to tell a flag followed by a
  positional from an option and its value. Requiring `=` makes every command
  line mean exactly one thing.
- **Bit tests need explicit parentheses.** `==` binds tighter than `&`, as in C,
  so `x & 1 == 1` means `x & (1 == 1)`.
- **Multi-line expressions need parentheses**, since a newline outside brackets
  ends a statement.

## Earlier — collections, 89 new builtins, platform layer, language server

### Added — Map<V> and bytes

`Map<V>` is a string-keyed hash map: open addressing, linear probing,
tombstones on removal, and rehashing at 70% load. Keys are copied on insert, so
a caller may reuse the string it passed. Keys are always `str`, which covers
symbol tables, headers, config, and JSON objects without needing a hash and
equality for arbitrary key types.

`bytes` is a mutable binary buffer, distinct from `str` because `str` is UTF-8
text that carries no NUL. Anything reading a file, hashing, or encoding needs
it.

### Added — 89 builtins

Text (17), math (21), collections (16), bytes (9), filesystem (11), time (8),
random (4), process (2). All wired through the C, native, and bytecode backends
and verified to produce identical results on each.

Two generators, deliberately separate: `random_int`/`random_float` are seeded
xorshift, reproducible and **predictable**; `random_bytes` reads OS entropy and
is the only one suitable for keys or tokens.

### Added — 10 stdlib modules

`std/collections/` (stack, queue, set, counter, sorting, search),
`std/text/` (strings, format), `std/data/` (hex, base64).

Sorting uses median-of-three quicksort with an insertion-sort cutoff, because a
first-element pivot degrades to O(n^2) on already-sorted input — the most common
real-world shape.

### Fixed

- **Generic functions over containers never worked.** `substitute()` had no case
  for `List` or `Map`, so `List<T>` was never replaced with `List<int>` in a
  specialization and any generic function taking a list failed to type-check.
  Found by writing `stack_push<T>(items: List<T>, value: T)`.

- **`builtin_type_to_type` case fallthrough.** New cases inserted mid-group made
  `AnyScalar`, `SameAsArgument`, and `AnyTask` fall through to `Bytes`, so
  `say(1)` reported "expected bytes".

### Not done

Encryption, HTTP, and GUI are absent for stated reasons — see `docs/stdlib.md`.
Encryption in particular is not an oversight: hand-rolled ciphers fail through
timing and padding side channels that tests pass straight through.

## Earlier — List<T>, platform layer, language server, and two crash fixes

### Added — `List<T>`

A growable sequence of any element type: `list<T>()`, `list_push`, `list_at`,
`list_put`, `list_size`, `list_pop`, `list_clear`.

Elements are 8-byte slots. Every PunPun value already fits one — an int, a
float's bits, a str pointer, or a handle to a boxed aggregate — so one runtime
implementation serves `List<int>`, `List<str>`, and `List<SomeStruct>` with no
templates and no per-type code. The compiler checks the element type
statically, so the runtime stores raw slots and repeats no check. Bounds *are*
checked, because an index is a runtime value.

Value semantics are preserved end to end: pushing an aggregate copies it in and
reading copies it out, so a stored element is independent of both the value
pushed and any value previously read. The C backend boxes aggregates into
allocated blocks; the native and bytecode backends deep-copy through their
existing copy machinery.

The element type comes from an explicit argument (`list<str>()`) or from
context (`let items: List<int> = list()`). Neither present is E0401.

`nums` is unchanged and remains the int-specific list for source compatibility.

Two bugs found while wiring the backends, both of which produced **silently
wrong answers** rather than errors:

- Native passed float elements in an SSE register, so the runtime stored a
  converted value instead of the original bits: `0.1` came back as something
  else entirely.
- Native did not copy aggregates in or out, so a list aliased the caller's
  struct and mutating the source changed the stored element.

Both are pinned by `tests/cases/list_value_semantics.pp`.



### Fixed

- **Stack overflow on recursive types.** `enum Expr { Add(Expr, Expr) }` crashed
  the compiler: `TypeContext::is_copy` and `needs_drop` walked members with no
  cycle detection. Both now carry a visited set, and a front-end check rejects
  types with no finite layout as **E0901** with an explanation. All three
  backends now give the same answer instead of one crashing and the others
  reporting an internal error. Pinned by `tests/cases/err_recursive_type.pp`,
  with `recursive_via_indirection.pp` guarding against false positives.

- **Duplicate `test-full` target** in the Makefile, and an optimization
  differential step that passed `--opt` — an option `run_tests.py` did not
  accept, so that step had never run. `--opt` is now implemented, and the
  differential genuinely runs the suite at -O0, -O1, and -O2.

### Added

- **Runtime platform layer.** `runtime/ppc_platform.h` with POSIX and Windows
  implementations. `ppcrt.c` now contains no `#ifdef`. The POSIX path is proven
  by the full suite, including concurrency and cancellation.
  **The Windows path has never been compiled** — see `docs/platform-support.md`
  and `tools/check-windows.sh`.

- **`host::rename_file`**, so the cache's atomic rename-over works on both
  platforms; plain `MoveFile` fails when the destination exists.

- **Language service** (`include/ppc/service/`, `src/service/`). The compiler's
  semantic information as a queryable API: diagnostics, hover, definition,
  document symbols, and semantic completion, built on the real lexer, parser,
  and checker rather than a second implementation.

- **LSP server**: `ppc serve --stdio`. Diagnostics carry their `E####` codes and
  secondary labels become `relatedInformation`. Completion is semantic — enum
  variants are offered qualified because a bare name would not compile. Only
  implemented capabilities are advertised.

- **`SourceManager` overlays**, so unsaved editor buffers are analysed instead
  of stale files on disk.

- **Minimal JSON** (`src/service/json.cpp`), keeping PPC dependency-free.

- **`make test-lsp`** — 17 protocol-level checks against the real binary.
  Also part of `make test-full`.

- **`docs/self-hosting.md`** — an assessment based on probing the language, with
  an ordered plan. PPC cannot be written in PunPun today; the blockers are
  named.

### Test status

62 cases across three backends, plus the sanitizer build, the optimization
differential at three levels, and 17 LSP checks. Zero build warnings.

## Earlier — compiler quality work toward PunPun 1.3

Everything below is implemented and tested. Items that were investigated but
not completed are in `docs/known-limitations.md`, not here.

### Added

- **Escape analysis** (`src/mir/escape.cpp`, `include/ppc/mir/escape.hpp`).
  A may-escape fixpoint over MIR that identifies aggregate allocations which
  cannot outlive their function. Identity objects are never promoted regardless
  of the result, because their semantics are defined by reference.

- **Frame promotion of non-escaping aggregates** in the native backend. A
  struct or enum proven local is built in frame storage instead of through
  `pp_object_alloc`. Frame storage is explicitly reinitialized on each
  execution, since a slot inside a loop is reused across iterations.

- **Copy elision** for freshly built, singly-read aggregates. The native
  backend deep-copies a value struct at every transfer; when the source is a
  fresh allocation with exactly one reader, the reader takes ownership instead.

- **Parameter usage analysis** (`analyze_parameters`), which identifies
  read-only parameters. Implemented and unit-covered, but **not yet consumed by
  any backend** — see known limitations.

- **Benchmark harness** (`tools/benchmark.py`) with median-of-N sampling, warmup
  runs, and BEFORE/AFTER comparison. Nine workloads in `tests/benchmarks/`
  chosen to isolate distinct costs: call overhead, allocation, string building,
  aggregate copying, checked arithmetic, tag dispatch, and monomorphization.

- **Recorded baseline** in `docs/benchmarks/baseline.md`, with the measurement
  method and this machine's noise floor stated explicitly.

- Regression test `tests/cases/escape_analysis.pp`, covering promotion inside
  loops, callee mutation isolation, genuine escape through return, nested value
  extraction, and the object-identity exclusion.

### Changed

- **Measured result:** native backend runtime improves by **74.9%** on
  `generic_heavy` and **45.3%** on `aggregates`. The native-vs-C aggregate gap
  narrows from 55.9x to 28.8x. Full numbers, including every figure that moved
  the wrong way, are in `docs/benchmarks/report-escape-analysis.md`.

### Fixed

- **README version drift.** The README claimed PunPun 0.6 while the compiler
  reported 1.0. `include/ppc/support/version.hpp` is the single source of truth;
  the README now matches it.

### Not done, deliberately

- `promoted_locals` in `PassStats` remains a **dead statistic** with no mem2reg
  behind it. Deleting the counter would have hidden the gap rather than closing
  it. See known limitations for why it was not implemented in this pass.
# 1.3.0

- Integrated PPC as PunPun's canonical compiler while preserving language and runtime ABI epoch 1.
- Added repository-level version synchronization, stable API metadata, release integration, and compatibility CLI aliases.
- Added verified HTTPS and native GUI runtime builtins shared by C, native x86-64, and bytecode programs.
- Connected the compiler-native LSP directly to the VS Code extension.
- Added cross-backend HTTPS/GUI regression coverage and the expanded standard library.
