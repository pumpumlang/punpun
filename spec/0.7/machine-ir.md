# PunPun 0.7 Machine IR contract

Status: implemented foundation in `0.7.0-dev.1`.

Machine IR is the first target-aware compiler representation below verified MIR. It does not define new PunPun source-language semantics. Its job is to make backend obligations explicit and verifiable.

## Pipeline position

```text
AST -> semantic analysis -> ownership -> HIR -> MIR -> Machine IR -> backend
```

HIR/MIR remain target-independent. Machine IR may contain target calling-convention, register and stack decisions.

## Initial target

The first Machine IR target is `x86_64-sysv`.

## Internal PunPun calling convention

Ordinary PunPun functions use the project-owned `punpun-block` convention:

- `RDI` points to a caller-built argument block.
- scalar/reference parameters occupy 8 bytes in declaration order;
- by-value record parameters occupy their full layout size in declaration order;
- a by-value record result reserves block offset `0` for a hidden destination pointer;
- when a hidden result pointer exists, ordinary parameters begin at offset `8`;
- scalar results, including the current float bit-pattern representation, return through `RAX`;
- void has no result location.

The direct x86 backend must consume these Machine IR offsets rather than independently recomputing them.

## Native extern ABI

`extern native` functions are described with the `sysv-amd64` convention. Machine IR records GPR/XMM/stack locations even when a particular backend still has a narrower supported subset. Backend limitations must be diagnosed or rejected honestly rather than silently changing the ABI.

## Liveness and allocation

Machine IR reconstructs target-aware live intervals from MIR values and marks conservative call barriers. Values that remain live across calls may not be assigned to caller-clobbered registers. Values used across basic-block boundaries currently receive conservative dedicated stack ranges, because block storage order is not sufficient to prove cross-CFG lifetime non-overlap.

Initial register policy:

- short-lived integer/pointer values may use `r10`, `r11`, `r8`, `r9`, `rcx`, `rdx`, then preserved registers;
- call-live integer/pointer values may use `r12`, `r13`, `r14`, `r15`, `rbx`;
- non-call-live floating values may use `xmm8` through `xmm15`;
- call-live floating values spill in this phase because SysV XMM registers are caller-clobbered;
- aggregate values use stack storage.

When all compatible registers are occupied, linear scan may evict the active compatible interval with the farthest end if the new interval ends sooner. Otherwise the new interval spills.

## Verification

Machine IR verification must reject at least:

- duplicate functions;
- non-canonical or invalid block targets;
- uses before definitions;
- values without locations;
- overlapping live values assigned to the same physical register;
- overlapping live stack ranges assigned to the same slot range;
- call-live values left in caller-clobbered registers;
- call-live floating values left in volatile XMM registers;
- stack allocations outside the declared frame;
- non-16-byte-aligned frame requirements;
- ABI parameter-count mismatches.

## Transitional boundary

As of `0.7.0-dev.5`, Machine IR is authoritative for direct-x86 function-body instruction selection as well as ABI/allocation state. The prior typed-source function-body emitter has been removed. Portable C/LLVM remain portability code-generation paths after the same semantic/ownership/HIR/MIR/Machine-IR authority chain.
