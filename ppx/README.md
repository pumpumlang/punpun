<p align="center"><img src="../ppx-site/static/ppx-mark.svg" width="92" alt="PPX"></p>

# PunPunXPac · PPX

<p align="center"><strong>Find, validate, publish, download, and install PunPun packages without creating a second build system.</strong></p>

PPX uses the same `Punpun.toml` and `Punpun.lock` graph consumed by `pp`. Registry packages are checksum-verified, extracted into the PPX cache, and then handed to the normal PunPun compiler as ordinary dependencies.

## Install a package

```sh
ppx search requests
ppx info requests
ppx install requests
ppx tree
pp run
```

`ppx add` remains an alias-style dependency workflow for local/path and registry packages. `ppx install <name> [requirement]` is the clearer consumer command; `ppx install` with no name refreshes the current graph.

## Publish your own package

A publishable `Punpun.toml` can contain:

```toml
[package]
name = "my_math"
version = "1.2.0"
description = "Small math helpers"
license = "MIT"
repository = "https://github.com/your-name/my_math"
homepage = "https://github.com/your-name/my_math/tree/main/docs"
readme = "README.md"
keywords = ["math", "helpers"]
entry = "src/main.pp"

[dependencies]
json = "^0.1.0"
```

Validate the exact archive without uploading anything:

```sh
ppx publish --dry-run
```

Create an account and authenticate. Passwords are prompted without echo by default:

```sh
ppx register developer
ppx login developer
```

Publish the immutable version:

```sh
ppx publish
# `ppx upload` is an equivalent spelling.
```

A registry independently re-reads `Punpun.toml`, verifies package name/version/dependencies, rejects unsafe ZIP paths and symlinks, enforces size/file-count limits, computes SHA-256, and refuses replacement of an existing version.

Path dependencies are intentionally rejected for public publication because `../something-local` cannot be reproduced on another user's machine. Replace them with registry version requirements before publishing.

## Download without installing

```sh
ppx download my_math 1.2.0
ppx download my_math '^1.2.0' -o vendor/my_math.zip
```

The downloaded ZIP is checked against the registry checksum before it is written.

## Commands

```text
ppx search <query>                search bundled + registry packages
ppx info <name>                   show owner, metadata, versions, checksums
ppx install [name] [requirement]  install a package, or refresh current graph
ppx add <name> [requirement]      add a registry/local dependency
ppx add <name> --path <dir>       add a local development dependency
ppx download <name> [requirement] download and verify an immutable ZIP
ppx remove <name>                 remove a dependency
ppx update                        refresh resolution and lock data
ppx tree                          print the dependency graph
ppx register <user>               create a registry account
ppx login <user>                  save a short-lived registry token
ppx logout                        revoke/remove the saved token
ppx publish [--dry-run]           validate and publish this package
ppx upload [--dry-run]            same publish flow, upload-oriented spelling
ppx yank <name> <version>         hide a version from new resolution
ppx cache [path|clean]            inspect or clear the cache
ppx doctor                        inspect PPX configuration/connectivity
```

## Package archive policy

PPX excludes known local/build junk while creating an upload:

```text
.git/  .punpun/  build/  dist/  node_modules/
__pycache__/  .pytest_cache/  .mypy_cache/  .ruff_cache/
*.pyc  *.tmp  .DS_Store  Thumbs.db  desktop.ini
```

Individual files over 16 MiB and archives over 32 MiB are rejected by the client. The reference registry also limits expanded size to 128 MiB and package file count to 4096.

## Registry selection

Set `PPX_REGISTRY` for network-backed operations:

```sh
export PPX_REGISTRY="http://127.0.0.1:8765"
```

Run the included reference registry locally:

```sh
python3 ppx-registry/server.py --host 127.0.0.1 --port 8765 --data .ppx-registry
```

The bundled server is a development/reference implementation, not falsely advertised as a production public service. A public deployment still needs production identity, TLS, durable object storage/database operations, moderation, backups, monitoring, and provenance/signing policy.

## Integrity model

- uploaded identity and dependency metadata come from the archive's own `Punpun.toml`;
- package versions are immutable;
- downloaded bytes must match the registry SHA-256;
- archive traversal, duplicate paths, symlinks, excessive expansion, and oversized archives are rejected;
- tokens live in the user's PPX config directory, never in project manifests;
- publication never executes uploaded PunPun source.
