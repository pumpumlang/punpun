# PunPun 0.7 FFI and ABI contract

## Stability level

This is the implemented 0.7 development ABI contract. It is inspectable with `ppc emit-abi`, but it is **not yet a 1.0 stable binary-compatibility promise**. Changes before 1.0 must remain versioned/tested and must invalidate native caches when ABI identity changes.

## PunPun-to-PunPun calls

The direct x86-64 backend uses the `punpun-block` convention:

- the caller constructs one byte-addressed argument block;
- `rdi` points to that block on entry;
- scalar/reference arguments occupy 8-byte argument-block slots;
- by-value records occupy their computed layout size;
- by-value record returns use a hidden destination pointer in the argument block;
- scalar results use the normal modeled result register (`rax`, or `xmm0` for floating values).

The offsets/sizes in Machine IR are authoritative. Native code generation must not recompute a competing layout from source syntax.

## `extern native` on x86-64 Linux

Native extern calls are modeled as `sysv-amd64`. Machine IR records supported GPR/XMM/stack locations and the native symbol. The backend must either implement the modeled ABI case or reject an unsupported case; it must not silently substitute a different calling convention.

## Visibility

Top-level functions are public by default. `private fn` is module-local and cannot be called from another imported source module. Object/field/method visibility continues to follow semantic visibility checks.

## Inspection

```sh
ppc emit-abi main.pp
ppc emit-machine-ir main.pp
```

These commands are intended for ABI audits, backend tests and integration development.

## Platform boundary

This document describes the currently implemented x86-64 SysV development target. Windows and future targets require their own qualified ABI lowering; a Linux build does not certify them.
