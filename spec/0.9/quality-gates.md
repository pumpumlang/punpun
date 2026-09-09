# PunPun 0.9 quality and performance gates

## Generated documentation

Public API reference output is derived from standard-library and first-party package declarations. `pp doc --check` must detect generated-output drift. Only explicitly marked `punpun doctest` / `punpun doctest-run` blocks participate in executable documentation testing.

## Fuzzing

The deterministic frontend mutation fuzzer must treat timeout, process signal and internal compiler error as failures and preserve a reproduction source. Ordinary user diagnostics for invalid mutated input are expected outcomes.

## Compatibility

The backend compatibility matrix compares observable exit status and standard output for direct x86-64, portable C and optional LLVM when available. A backend is not marked passed unless it was actually executed.

## Stress

The structured-async stress gate repeats task-group programs across direct and portable-C backends. CI uses bounded iteration counts; larger local/release counts are supported.

## Profile-guided optimization

0.9 PGO support is explicitly scoped to the portable-C backend. The training and optimized compile must reuse stable generated file/profile paths. Direct-x86 PGO is outside this contract.
