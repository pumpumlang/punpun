# Publishing Packages with PPX

Publish an immutable package version to a PPX registry, then install it from another project.

## Overview

A publish has four stages: read metadata from `Punpun.toml`, validate dependencies and files, create a checksummed ZIP, then upload it with your authenticated PPX token. The registry independently validates the archive before accepting it.

## Syntax

A publishable manifest can include:

```toml
[package]
name = "my_math"
version = "1.2.0"
description = "Small math helpers"
license = "MIT"
repository = "https://example.invalid/my_math"
homepage = "https://example.invalid/my_math/docs"
readme = "README.md"
keywords = ["math", "helpers"]
entry = "src/main.pp"

[dependencies]
json = "^0.1.0"
```

Package names and versions are validated, the readme path must stay inside the package, dependency requirements must be valid version requirements, and unusually large or unsafe archive paths are rejected.

## Runnable example

Start the development registry:

```sh
python3 ppx-registry/server.py --host 127.0.0.1 --port 8765 --data .ppx-registry
```

In another terminal:

```sh
export PPX_REGISTRY=http://127.0.0.1:8765
ppx register developer
ppx login developer
cd my-package
ppx publish --dry-run
ppx publish
```

Install it from a different project:

```sh
ppx install my_math '^1.2.0'
ppx info my_math
```

Download the immutable archive without modifying a project:

```sh
ppx download my_math 1.2.0 -o my_math-1.2.0.zip
```

## Common mistakes

- Putting a token in `Punpun.toml`. PPX stores login state outside the project.
- Publishing secrets, build output, `.git`, `.punpun`, `node_modules`, or cache directories. The client excludes known junk, but authors are still responsible for package contents.
- Depending on `../local-package` with a path dependency in a public release. Replace it with a registry version requirement first.
- Expecting to overwrite `1.2.0`. Publish `1.2.1` or another new version; use `ppx yank` only when an existing release should no longer resolve normally.

## Next steps

Use `ppx info <name>` to inspect owner, versions, checksums, dependencies, and metadata. Use `ppx doctor` to verify registry connectivity and local cache paths.
