# Contributing to PunPun

PunPun is a compiler/toolchain project, so changes are judged by observable behavior, diagnostics, tests and reproducibility rather than file count. Small reviewable changes are preferred over broad rewrites that silently alter language semantics.

## Build and test

On a Linux development host with C17/C++17 compilers, Python 3, Node.js and the runtime development dependencies installed:

```sh
make -j2 test
```

Useful focused gates:

```sh
python3 scripts/check_version.py
python3 scripts/privacy_audit.py .
python3 scripts/check_links.py README.md docs compiler/docs spec examples editors/vscode/README.md
./selfhost/bootstrap.sh
python3 scripts/docgen.py --check
python3 scripts/doctest.py --ppc ./build/ppc
python3 scripts/compat_matrix.py --quick
python3 scripts/fuzz_frontend.py --iterations 60 --seed 20560
python3 scripts/stress.py --quick
python3 scripts/benchmark_projects.py --modules 10 --rounds 1 --gate
```

For portable-C optimization changes, use `pp pgo` on a controlled training workload.

## Scope of this repository

This repository is the language: compiler, runtime, standard library,
first-party packages, specification and editor integration. Documentation lives
in [`punpun-docs`](https://github.com/pumpumlang/punpun-docs) and the package
manager in [`punpun-ppx`](https://github.com/pumpumlang/punpun-ppx); send
changes to those there. A language change that needs a documentation change
needs a pull request in each repository.

## Language/runtime changes

A language or runtime feature is not complete at parsing. A normal vertical change covers, where applicable:

1. syntax/parser;
2. semantic/type checking;
3. ownership/borrow behavior;
4. HIR, MIR and Machine IR lowering/verification;
5. every affected backend;
6. runtime/ABI behavior;
7. diagnostics and negative cases;
8. runnable tests/examples;
9. specification and learning documentation.

Structured-concurrency changes must test cancellation and direct/portable-C behavior. Backend changes must preserve the Step 7 Machine-IR-only direct body path.

## Generated content

Do not hand-edit generated API reference files. Change the library/package source and run:

```sh
pp doc
pp doc --check
```

Do not hand-edit generated brand assets. Change `scripts/build_brand.py`, regenerate twice and require byte-identical output.

Timestamped backups such as `*.bak-*` do not belong in the repository; Git already volunteered for that job.

## Pull requests

Keep commits scoped and explain observable behavior. CI on pushes and pull requests must remain green. Platform-specific installer/package claims require the corresponding real platform qualification job; Linux cannot certify Windows MSI execution or a real Arch pacman transaction.

## 1.x compatibility rule

Run `make stability` before public language/runtime/stdlib API changes. Removing/changing frozen 1.0 symbols or ABI/package/lockfile epoch 1 is a major-version decision.
