# PunPun engineering rules

These rules apply to every compiler, runtime, package, documentation and release change.

## Release truthfulness

- Never represent an archive, placeholder or cross-target guess as a native binary.
- Claim Windows, Arch/CachyOS or another platform only after the corresponding workflow or real host passes.
- Keep implemented behavior, reserved syntax and roadmap work visibly separate.
- Every compiler/runtime bug fix requires a regression test.
- Native behavior changes require direct x86-64 and portable C coverage where both paths apply.

## Privacy

- Keep the repository identity-neutral.
- Do not add personal names, usernames, email addresses, account handles, home-directory paths, tokens, credentials, private keys, shell history or machine-specific metadata.
- Run `python3 scripts/privacy_audit.py .` before packaging.

## Versioning and releases

- `VERSION` is the only hand-edited product-version source.
- Run `python3 scripts/sync_version.py` after changing `VERSION`.
- Run the full test, self-host, generated-doc/doctest and release gates before publishing.
- Development milestones use prerelease versions; do not label unfinished work stable.

## Compatibility/backend invariants

- Preserve valid 0.6+ source unless a documented migration and diagnostic are provided.
- ABI/ownership/overload changes must be specified before implementation.
- Direct x86 PunPun function bodies remain Machine-IR-only.
- Direct-native incremental correctness must preserve per-function dependency invalidation and the exact-one-function rebuild gate.

## Step 8 concurrency/debugging gates

- Structured task groups must have direct/C regression coverage and deterministic cleanup.
- Cancellation is cooperative; blocking/safe-point behavior must be documented rather than presented as preemptive cancellation.
- Async networking documentation must distinguish worker-task concurrency from true nonblocking socket/event-loop I/O.
- Source debugging must preserve deterministic source mapping and cannot depend on machine-specific absolute paths in committed output.

## Step 9 ecosystem/quality gates

- New PPX archives must remain deterministic and contain a valid internal integrity manifest.
- Registry transport must remain HTTPS-by-default for non-loopback endpoints.
- Generated API docs must pass `scripts/docgen.py --check` and explicit doctests must pass.
- Fuzz/compatibility/stress gates must remain deterministic enough for CI and preserve a useful reproduction on internal compiler failure.
- `pp pgo` is portable-C PGO until direct-native PGO is separately implemented and validated.
