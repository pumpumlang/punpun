# PunPun roadmap

PunPun is being developed toward Java-level reliability and C++-class native performance without hiding platform limitations or unfinished compiler work.

## 0.6 — language foundations

| Step | Scope | State |
| --- | --- | --- |
| 1 | Canonical versioning, green baseline, frozen generic/enum/nullability/ownership rules, parser scaffolding and compatibility gates | Complete in `0.6.0-dev.1` |
| 2 | Generic declarations/calls, constraints, monomorphization and deterministic specialization caching | Complete in `0.6.0-dev.3` |
| 3 | Algebraic enums, `Option<T>`, `Result<T,E>`, nested destructuring, exhaustive matching and `?` | Complete in `0.6.0-dev.3` |
| 4 | Move-state dataflow, lexical destruction, stronger borrows and checked `Slice<int>` views | Complete in `0.6.0-beta` |
| 5 | Mandatory verified HIR → MIR pipeline, MIR-authoritative function scheduling, function/interface/body fingerprints | Complete in `0.6.0-beta` |
| 5.5 | Optional Clang/LLVM compatibility backend and LLVM IR emission without replacing the native PunPun frontend | Complete in `0.6.0-beta` |
| 6 | Standard-library/runtime integration, compatibility regression gate, self-host verification, release qualification and packaging | Complete on the Linux x86-64 beta release host |

The normative 0.6 decisions live in [`spec/0.6/`](spec/0.6/).

## Step 7 / PunPun 0.7 — compiler scalability and backend maturity

Step 7 is implementation-complete in `0.7.0-dev.5` on the Linux x86-64 development host.

### Phase 7.1 — Machine IR and explicit ABI — complete
- Verified target-aware Machine IR below MIR.
- Explicit PunPun argument-block and SysV AMD64 ABI locations, call barriers, liveness, physical locations, spills and frame verification.
- `ppc emit-machine-ir` and `ppc emit-abi` expose compiler/ABI state.

### Phase 7.2 — backend decoupling — complete
- Canonical lexical binding IDs and explicit move/drop/aggregate/address/index/list/await IR operations.
- Direct x86-64 PunPun function bodies are emitted exclusively from verified Machine IR.

### Phase 7.3 — granular incremental compilation — complete
- Independent native function objects and entry glue.
- Interface/body/direct-dependency ABI fingerprints and exact cache hit/miss reasons.
- CI scalability gate requires one edited function to rebuild exactly one function in the generated workload.

### Phase 7.4 — optimizer and allocation maturity — complete
- Machine-IR propagation/cleanup, branch simplification and unreachable-block pruning.
- Call-safe physical allocation, callee-save handling and safe spill-range reuse.

### Phase 7.5 — everyday compiler ergonomics — complete
- Stable diagnostics/fix-its, module-private boundaries, FFI/ABI documentation, lint gates and deterministic PPX archives.

## Step 8 / PunPun 0.8 — structured async, networking and debugging

Step 8 is implementation-complete as part of `0.9.0-dev.5` on the Linux x86-64 development host. Step numbering describes engineering milestones; the combined development version records that Steps 8 and 9 were completed in one validated development pass.

### Phase 8.1 — structured task groups and cancellation — complete
- Runtime task groups own an explicit set of child tasks and expose add, wait, timed wait, pending, done, cancel and close operations.
- Group cancellation propagates a cooperative cancellation request to every member.
- `sleep_ms` is a cancellation safe point and wakes promptly instead of forcing a cancelled task to sleep for its original full duration.
- Group handles are runtime-owned, validated and cleaned deterministically without changing individual task lifetime ownership.
- Direct and portable-C regression tests cover task groups and cancellation wake-up behavior.

### Phase 8.2 — async I/O and networking ergonomics — complete
- `std::async` includes async text-file helpers and cancellable delay helpers.
- The first-party `requests` package exposes async request/get/post/put/patch/delete/head wrappers that compose with PunPun tasks and task groups.
- The implementation deliberately reuses one HTTP stack instead of introducing a second networking runtime.
- Current networking calls still execute their synchronous libcurl operation inside a PunPun worker task; they are concurrent at the task level, not an event-loop/nonblocking-socket implementation. That boundary is documented rather than hidden.

### Phase 8.3 — source debugging foundation — complete
- `pp debug-map` generates deterministic JSON mappings from emitted assembler `.file`/`.loc` directives to PunPun functions and source locations.
- `pp debug` builds a debug binary and launches GDB or LLDB when available.
- Direct backend source-location directives are now exposed as a supported developer workflow rather than remaining invisible assembler detail.

### Phase 8.4 — platform qualification architecture — complete, qualification remains platform-specific
- Fast CI continuously covers compiler, generated docs/doctests, compatibility, fuzzing, structured-async stress, incremental compilation and hygiene.
- The Arch release job now executes package qualification inside a current Arch Linux Docker environment on the GitHub Ubuntu runner to avoid runner-tooling assumptions inside a minimal job container.
- Linux release qualification runs Step 8/9 compatibility, fuzz and structured-concurrency gates before artifact assembly.
- Windows and Arch are not called qualified until their real platform workflows pass. Workflow architecture is complete; platform success remains an observed result, not a roadmap checkbox.

## Step 9 / PunPun 0.9 — ecosystem, security and production hardening

Step 9 is implementation-complete in `0.9.0-dev.5` on the Linux x86-64 development host.

### Phase 9.1 — PPX integrity and transport hardening — complete
- New PPX archives contain deterministic `PPX-MANIFEST.json` metadata with per-file path, size and SHA-256 digests.
- `ppx verify <archive>` validates the internal package manifest and rejects tampered package content.
- Download/install paths verify the internal manifest when present while remaining compatible with legacy registry archives.
- Registry configuration requires HTTPS for non-loopback endpoints unless the user deliberately enables the insecure-development override.
- `ppx audit [--deny-injection]` audits materialized dependencies and can reject native `@inject->` use in dependency source.
- This is an integrity/transport model, not public-key package signing. Cryptographic publisher identity remains a later security project.

### Phase 9.2 — generated API documentation and doctests — complete
- `pp doc` deterministically derives API documentation from the standard library and first-party package source.
- `pp doc --check` fails when generated API docs drift from compiler/library source.
- `pp test --doc` and `scripts/doctest.py` compile/run explicitly marked PunPun documentation examples.
- Generated API Markdown/JSON and the documentation-site API reference are CI-gated.

### Phase 9.3 — fuzzing, backend compatibility and stress gates — complete
- Deterministic mutation fuzzing drives the real compiler frontend and records a reproduction on timeout, signal or internal compiler failure.
- The compatibility matrix compares direct x86-64, portable C and optional LLVM behavior for common language fixtures.
- The async/compiler stress harness repeatedly alternates direct and C task-group execution to catch state leaks and flaky concurrency.
- CI uses bounded versions of these gates; longer local/release runs remain available through `pp fuzz`, `pp compat` and `pp stress`.

### Phase 9.4 — measured performance tooling and PGO — complete for the portable-C backend
- `pp pgo` builds an instrumented portable-C program, runs a training workload and rebuilds with compiler profile feedback.
- The PGO helper uses stable generated C/object/profile paths so GCC/Clang profile data can be reused reliably.
- Direct-x86 PGO is not claimed; the current feature is explicitly scoped to PunPun's portable-C backend.

### Phase 9.5 — integrated quality gates and release truthfulness — complete
- `Makefile` and CI expose generated-doc, doctest, fuzz, compatibility and structured-async stress gates.
- Step 8/9 features have dedicated end-to-end regression coverage in `tests/test_step8_9.py`.
- The source/release privacy, reproducibility and qualify-before-promote rules remain mandatory.

## Step 10 / PunPun 1.0 — stable compatibility line

Step 10 is implementation-complete in `1.0.0` on Linux x86-64.

### Phase 10.1 — language/specification freeze — complete
- Stable language epoch 1.0, SemVer/deprecation guarantees and `stable-api.json`.
- `pp stable-check` rejects removal/signature drift of frozen builtins and standard-library APIs.

### Phase 10.2 — ABI/package compatibility — complete
- Language ABI 1, runtime ABI 1, package format 1 and lockfile format 1 are explicit metadata.
- New manifests declare `language = "1.0"` and `abi = 1`; incompatible requirements are rejected.

### Phase 10.3 — platform support tiers — complete
- Tier 1 Linux x86-64; Tier 2 Arch/Windows x86-64; Tier 3 unsupported macOS/ARM64.
- `pp platform-info` exposes the machine-readable policy.

### Phase 10.4 — release integrity/signing — complete
- Deterministic assembly, SHA-256, source SBOM and provenance.
- Optional Ed25519 release signatures plus PPX detached signatures/trust roots.

### Phase 10.5 — stable release gates — complete
- ABI/stability/platform-policy/signing regression tests are integrated with test/CI/release gates.

## Post-1.0 candidates requiring separate design review

 public-key PPX signing/trust roots, native async event loop, direct-x86 PGO/autovectorization/SIMD, hygienic derive/macros, ARM64/macOS backends, complete GUI infrastructure and broader full-language self-hosting.

A feature is promoted only when its complete parser/semantics/ownership/IR/backend/tooling/test/documentation path is implemented where applicable. Platform qualification is never inferred from another platform's successful run.
