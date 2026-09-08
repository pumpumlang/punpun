# Packages and PPX

PPX finds, validates, downloads, publishes, and installs PunPun packages while `pp` remains responsible for compilation.

## Overview

Projects use `Punpun.toml` and `Punpun.lock`. PPX materializes registry packages into the same dependency graph used by `pp`, so package installation does not create a second compilation model.

## Syntax

Search and install a package:

```sh
ppx search requests
ppx install requests
ppx tree
```

Inspect package metadata:

```sh
ppx info requests
```

## Runnable example

Create a project, add the bundled requests package, then run it:

```sh
pp new package-demo
cd package-demo
ppx install requests
pp run
```

For a package you are authoring, validate the archive before upload:

```sh
ppx publish --dry-run
```

Then authenticate and publish:

```sh
ppx register developer
ppx login developer
ppx publish
```

## Common mistakes

- Publishing from a directory without `Punpun.toml`.
- Reusing an already-published version number. Registry versions are immutable; bump the package version instead.
- Publishing path-only dependencies. Public package metadata must use registry version requirements so another machine can reproduce the graph.
- Assuming `ppx install` executes package source during download. PPX validates and materializes source; compilation still happens through `pp`.

## Next steps

Read the [PPX publishing guide](ppx-publishing.html) for metadata fields, validation rules, account flow, downloads, yanking, and local registry testing.
