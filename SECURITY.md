# PunPun security policy

## Supported releases

PunPun is currently prerelease software. Security fixes are applied to the newest published prerelease and to `main`; older prereleases may be superseded rather than patched indefinitely.

## Reporting a vulnerability

Prefer GitHub's private security-advisory reporting flow for the `pumpumlang/punpun` repository when it is available. Include the affected PunPun version/commit, target platform, minimal reproduction, impact, and whether PPX/package content is involved.

If private advisory reporting is unavailable, open a minimal public issue requesting a private security contact **without posting exploit details, credentials, tokens, private package data, or a working weaponized proof of concept**.

Compiler crashes are ordinary bugs unless they cross a trust boundary, corrupt memory in tooling that processes untrusted input, bypass package integrity, or otherwise create a security impact. PPX/package-manager reports should identify whether malicious registry metadata, archives, path traversal, symlinks, checksums, authentication, or install scripts are involved.

## Release integrity

PunPun release publication follows build → qualify → promote. Source and artifacts are checksummed; release promotion must not occur until the exact candidate commit passes required Linux, Arch and Windows qualification. A failed platform gate is a failed candidate, not a decorative warning.
