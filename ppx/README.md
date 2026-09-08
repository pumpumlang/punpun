<p align="center"><img src="../ppx-site/static/ppx-mark.svg" width="92" alt="PPX"></p>

# PunPunXPac · PPX

<p align="center"><strong>Packages for PunPun, without a second build system.</strong></p>

PPX discovers, resolves and caches PunPun packages using the same `Punpun.toml` and `Punpun.lock` files consumed by `pp`.

> **Works offline by default.** The seven first-party packages and the public catalog snapshot require no local registry process. Network publishing remains an explicitly configured development/hosted-registry operation.

## Start here

```sh
ppx search requests
ppx add requests
ppx tree
pp run
```

The SDK already includes these first-party packages:

| Package | Purpose |
| --- | --- |
| `requests` | Native HTTP requests through the system libcurl runtime |
| `json` | JSON validation and field extraction |
| `gui` | PunUI native GUI foundation |
| `filesystem` | Filesystem and path conveniences |
| `logging` | Small structured logging helpers |
| `cli` | Command-line argument helpers |
| `testing` | Lightweight test assertions |

Bundled packages work offline. PPX first checks the configured package source and keeps downloaded artifacts in a checksum-verified cache.

## Resolution pipeline

```text
Punpun.toml → version selection → checksum cache → path graph → normal PunPun compiler
```

PPX does not introduce a second compiler or hidden package build format. The exact resolved graph is recorded in `Punpun.lock`.

## Commands

```text
ppx search <query>             find packages
ppx info <name>               show package metadata
ppx add <name> [requirement]  resolve and add a dependency
ppx remove <name>             remove a dependency
ppx tree                      print the dependency graph
ppx fetch                     materialize locked dependencies
ppx update                    refresh resolution and lock data
ppx publish                   publish the current package
ppx yank <name> <version>     hide a version from new resolution
ppx login <user> <password>   authenticate to a registry
ppx logout                    remove the saved token
ppx doctor                    inspect configuration and cache state
```

## Registry selection

Set `PPX_REGISTRY` when you want network-backed search, download or publishing:

```sh
export PPX_REGISTRY="https://registry.example/api"
ppx search json
```

The reference API in `../ppx-registry/` is intended for development and acceptance tests. There is no false claim that it is already a production public service.

## Integrity model

- downloaded archives must match the registry's SHA-256 checksum;
- paths are validated before extraction;
- immutable versions prevent replacement after publication;
- yanking changes resolution visibility without deleting history;
- credentials are stored in the user's configuration directory, never in a project manifest.

PPX is beta software. Production provenance signing, organizational accounts, full conflict solving and hosted service operations remain tracked work.

## Development checks

```sh
ppx doctor
python3 -m unittest tests.test_ecosystem -v
python3 ppx-site/build.py
```

The product version is read from the repository-root `VERSION`; package versions remain independently declared in each package's `Punpun.toml`.
