# Contributing to PunPun

PunPun is a compiler/toolchain project, so changes are judged by behavior, diagnostics, tests, and release reproducibility rather than file count. Small, reviewable changes are preferred over broad rewrites that silently alter language semantics.

## Build and test

On a Linux development host with a C17 compiler, C++17 compiler, Python 3, Node.js, zstd, and the runtime development dependencies installed:

```sh
make -j2 test
```

Useful focused gates are:

```sh
python3 scripts/check_version.py
python3 scripts/privacy_audit.py .
python3 scripts/check_links.py README.md docs docs-site/content examples editors/vscode/README.md
./selfhost/bootstrap.sh
python3 docs-site/build.py
python3 ppx-site/build.py
python3 scripts/package_vsix.py
./build/ppc emit-machine-ir main.pp
```

Release assembly additionally requires Pillow because `scripts/build_brand.py` regenerates all PP raster/ICO assets from the canonical geometry before packaging.

## Language changes

A language feature is not complete at parsing. A normal vertical change should cover, where applicable:

1. syntax/parser;
2. semantic/type checking;
3. ownership/borrow behavior;
4. HIR, MIR, and Machine IR lowering/verification where the feature reaches target code;
5. every supported backend affected by the feature, consuming Machine IR rather than rebuilding ABI facts independently;
6. diagnostics and negative cases;
7. runnable tests/examples;
8. specification and learning documentation.

Compatibility changes must be intentional and documented. Do not silently weaken the safe-language rules to make one example compile.

## Generated branding

Do not hand-edit generated PunPun logos/icons. Edit `scripts/build_brand.py`, then run:

```sh
python3 scripts/build_brand.py --repo-root .
```

A second run must produce identical bytes. Timestamped backups such as `*.bak-*` do not belong in the repository; Git is the history mechanism, because apparently one history mechanism was not enough for civilization.

## Pull requests

Keep commits scoped, explain observable behavior, and include tests. CI on pushes and pull requests must remain green. Platform-specific installer/package claims require the corresponding real platform qualification job; a Linux host cannot certify Windows MSI execution or a real pacman transaction.
