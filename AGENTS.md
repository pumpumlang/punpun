# PunPun engineering rules

These rules apply to every compiler, runtime, package, documentation and release change.

## Release truthfulness

- Never represent an archive, placeholder or cross-target guess as a native binary.
- Claim Windows, Arch/CachyOS or other platform validation only after the corresponding workflow or real host completes it.
- Keep implemented behavior, reserved syntax and roadmap work visibly separate.
- Every compiler bug fix requires a regression test.
- Native code changes require direct x86-64 and portable C coverage where both paths apply.

## Privacy

- Keep the repository identity-neutral.
- Do not add personal names, usernames, email addresses, account handles, home-directory paths, tokens, credentials, private keys, shell history or machine-specific metadata.
- Use neutral examples and runtime discovery instead of account-specific values.
- Run `python3 scripts/privacy_audit.py .` before packaging.

## Versioning and releases

- `VERSION` is the only hand-edited product version source.
- Run `python3 scripts/sync_version.py` after changing `VERSION`.
- Run `make test`, `make selfhost` and `python3 scripts/release.py` before publishing.
- Development milestones use prerelease versions; do not label unfinished work as stable.

## Compatibility

- Preserve valid 0.6 source throughout the 0.7 cycle unless a documented migration and diagnostic are provided.
- Language rules that affect ABI, ownership or overload selection must be specified before implementation.
- Backend or ABI changes must pass through Machine IR, update its verifier/specification when the contract changes, and include a regression test that exercises the affected lowering.
- Deterministic output and reproducible tests are release requirements.

## Step 7 scalability/reproducibility gates

- Direct-native incremental changes must preserve per-function dependency correctness and pass `python3 scripts/benchmark_projects.py --modules 10 --rounds 1 --gate`.
- PPX package publishing must remain byte reproducible for identical file bytes regardless of mtimes.
- Direct x86 function bodies must remain Machine-IR-only; do not reintroduce a typed-source backend fallback.
