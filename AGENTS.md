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

- Preserve valid 0.5 source throughout the 0.6 cycle unless a documented migration and diagnostic are provided.
- Language rules that affect ABI, ownership or overload selection must be specified before implementation.
- Deterministic output and reproducible tests are release requirements.
