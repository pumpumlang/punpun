# PunPun 1.0.0 release notes

PunPun 1.0 establishes the first stable compatibility line.

## Stability
- Stable language epoch **1.0** with SemVer/deprecation guarantees.
- `pp stable-check` enforces the frozen builtin/stdlib surface.
- Explicit ABI 1, runtime ABI 1, package format 1 and lockfile format 1.
- New projects record `language = "1.0"` and `abi = 1`.

## Release/security
- `pp platform-info` exposes support tiers.
- Source SBOM and release provenance generation.
- Optional Ed25519 release signatures and PPX detached signatures/trust roots.
- Release qualification now pins WiX 6 with the correct BootstrapperApplications extension and installs the Arch MIME/icon runtime dependencies before package lifecycle testing.

## Existing foundations
1.0 includes ownership/algebraic types, verified HIR/MIR/Machine IR, native/C/LLVM backends, incremental compilation, structured async/cancellation, source debugging, PPX, docs/doctests, fuzz/compat/stress gates and portable-C PGO.

Linux x86-64 is Tier 1. Arch and Windows x86-64 are Tier 2 and are called qualified per-release only when their real workflows pass.
