# PunPun 0.6 compatibility contract

## Source compatibility

Valid 0.5 modern syntax must remain valid throughout 0.6. The temporary 0.4 migration dialect remains accepted for this cycle but may emit deprecation guidance later. A removal requires a migration tool, a dedicated diagnostic and release-note notice.

New keywords (`enum`, `match`, `case`, `where`) are reserved in 0.6. Programs that used them as identifiers receive a reserved-word diagnostic. This is the only intentional source reservation in Step 1.

## Behavioral compatibility

- Evaluation order stays left-to-right.
- `and`/`or` remain short-circuiting.
- Integer arithmetic remains checked unless an explicit wrapping operation is used.
- Existing concrete object method dispatch remains static.
- Existing `nums` assignment/parameter passing retains shared-handle alias behavior.
- Existing named/default argument behavior is preserved.
- Safe code cannot acquire raw-pointer behavior implicitly.

## ABI and serialized data

The 0.x native ABI is not stable across compiler versions. Build caches and generic specializations include the compiler version and target ABI. `Punpun.lock`, package manifests and PPX checksums remain deterministic and forward-readable unless a schema version is explicitly changed.

## Required gates

Every 0.6 step must pass:

1. all existing 0.5 tests;
2. modern and migration-dialect compatibility fixtures;
3. version-surface consistency;
4. direct/C backend parity where applicable, plus LLVM-backend parity when Clang is present;
5. self-host fixed-point verification;
6. privacy and release-artifact validation.
