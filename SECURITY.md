# PunPun security policy

## Supported releases

PunPun is prerelease software. Security fixes are applied to the newest published prerelease and to `main`; older development snapshots may be superseded instead of receiving long-term patch branches.

## Reporting a vulnerability

Prefer GitHub's private security-advisory reporting flow for `pumpumlang/punpun`. Include the affected PunPun version/commit, target platform, minimal reproduction, impact, and whether PPX/package content is involved.

If private advisory reporting is unavailable, open a minimal public issue requesting a private security contact **without posting exploit details, credentials, tokens, private package data, or a weaponized proof of concept**.

Compiler crashes are ordinary bugs unless they cross a trust boundary, corrupt memory in tooling processing untrusted input, bypass package integrity, or otherwise create security impact.

## PPX/package integrity

Current Step 9 protections include:

- SHA-256 verification of downloaded registry archives;
- deterministic internal `PPX-MANIFEST.json` path/size/SHA-256 verification for new packages;
- path-safe archive extraction checks;
- HTTPS required for non-loopback registries unless `PPX_ALLOW_INSECURE_REGISTRY=1` is explicitly set for development;
- `ppx verify` for offline package checks;
- `ppx audit --deny-injection` for rejecting native `@inject->` usage in materialized dependencies.

These mechanisms detect corruption/tampering and reduce accidental insecure transport. They **do not authenticate publisher identity with public-key signatures**. Public-key package signing, key rotation and trust-root policy remain future security work and must not be implied by the current manifest format.

## Release integrity

PunPun release publication follows build → qualify → promote. Source and artifacts are checksummed; release promotion must not occur until the exact candidate commit passes the required platform qualification. A failed platform gate is a failed candidate, not a decorative warning.

## Release and package signatures

PunPun 1.0 supports detached Ed25519 release and PPX signatures. Private signing keys must never be committed or placed in publisher bundles.
