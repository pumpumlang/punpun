<p align="center">
  <img src="assets/punpun-logo.svg" alt="PunPun programming language" width="720">
</p>

<p align="center">
  <strong>Native by default. Fast to iterate. Explicit when you need the metal.</strong><br>
  PunPun is an ahead-of-time programming language with compiler-backed tooling, deterministic ownership foundations, modern algebraic types, and an optional LLVM code-generation path.
</p>

<p align="center">
  <img alt="PunPun 1.0.0" src="https://img.shields.io/badge/version-1.0.0-b9ff4a?style=for-the-badge&labelColor=11151e">
  <img alt="Linux x86-64 validated" src="https://img.shields.io/badge/validated-Linux%20x86--64-66e3ff?style=for-the-badge&labelColor=11151e">
  <img alt="MIT license" src="https://img.shields.io/badge/license-MIT-f6f7fa?style=for-the-badge&labelColor=11151e">
</p>

<p align="center">
  <a href="#install">Install</a> ·
  <a href="#quickstart">Quickstart</a> ·
  <a href="#language-at-a-glance">Language</a> ·
  <a href="#ppx-packages">PPX</a> ·
  <a href="#documentation">Docs</a> ·
  <a href="#contributing">Contributing</a>
</p>

---

## What PunPun is

PunPun compiles `.pp` source through one semantic pipeline and produces native executables. The normal Linux x86-64 path does not require an interpreter or VM.

```text
.pp source
   ↓
parser + semantic analysis
   ↓
ownership / borrow analysis
   ↓
typed HIR
   ↓
verified MIR
   ↓
Machine IR (ABI + allocation)
   ↓
┌─────────────────────────────┐
│ direct PunPun x86-64 backend│
│ portable C backend          │
│ optional LLVM/Clang backend │
└─────────────────────────────┘
   ↓
native executable
```

The project values measurable compiler/runtime behavior over benchmark folklore. If a number is not measured for a named workload, it is not presented as a universal performance claim.

### PunPun 1.0 stable compatibility snapshot

PunPun 1.0 keeps the completed compiler/runtime work from Steps 1-9 and adds the first enforceable stable compatibility contract:

- **Language stability:** language epoch `1.0`, semantic-versioning/deprecation policy, and `pp stable-check` against the frozen builtin/stdlib surface.
- **ABI/package stability:** language ABI 1, runtime ABI 1, package format 1, and lockfile format 1 are explicit compiler metadata and project requirements.
- **Stable projects:** new manifests record `language = "1.0"` and `abi = 1`; incompatible requirements fail before compilation.
- **Platform promises:** `pp platform-info` exposes Tier 1 Linux x86-64, Tier 2 Arch/Windows x86-64, and unsupported Tier 3 targets without pretending one platform's success proves another.
- **Release provenance:** deterministic release inputs, SHA-256 checksums, source SBOM/provenance, and optional detached Ed25519 release signatures.
- **PPX publisher identity:** deterministic package manifests remain, with optional detached Ed25519 package signatures and local public-key trust roots.

The earlier foundations remain available: generics, algebraic enums, `Option`/`Result`, exhaustive match, ownership/borrows, checked slices, typed HIR → verified MIR → verified Machine IR, per-function native caching, direct Linux x86-64, portable C, optional LLVM, structured async/cancellation, source debugging, LSP/VS Code, PPX, docs/doctests, fuzz/compat/stress gates, PGO and self-host bootstrap verification.

> **Stable does not mean imaginary platform support.** Linux x86-64 is the Tier-1 1.0 implementation/validation host. Arch and Windows x86-64 are Tier 2 and are called qualified for a particular release only when their real release-tag workflows pass. Async HTTP still uses worker-task concurrency around synchronous libcurl, and portable-C PGO is not direct-x86 PGO.

## Install

The commands below refer to the `1.0.0` stable source/release line. Platform-specific artifacts remain subject to the support tiers and real release-tag qualification gates.

### Linux x86-64 installer

Use the self-extracting release installer:

```sh
bash PunPun-1.0.0-Linux-x86_64-Installer.run
```

It installs the SDK under `~/.local/share/punpun`, command wrappers under `~/.local/bin`, VS Code support when an editor CLI is available, and the `.pp` Linux MIME/file icon association.

Verify the installation:

```sh
pp --version
pp doctor
```

### Arch / CachyOS

Install the generated package with pacman:

```sh
sudo pacman -U punpun-1.0.0-1-x86_64.pkg.tar.zst
```

The package includes the PunPun MIME definition and hicolor file icons for `.pp` source files.

### Portable SDK

Extract `PunPun-1.0.0-linux-x86_64-SDK.zip` or `PunPun-1.0.0-linux-x86_64.tar.zst` and invoke the commands from its `bin/` directory.

### Windows

The repository includes a WiX v4 MSI/Burn installer project under [`installers/windows/`](installers/windows/). It owns PATH and `.pp` file-type/icon registration so upgrade/uninstall remain reversible. The source tree includes the Windows installer project; a Windows binary is considered release-qualified only when the Tier-2 Windows workflow actually builds/installs/uninstalls it successfully.

## Quickstart

Create and run a project:

```sh
pp new hello
cd hello
pp run
```

Replace `src/main.pp` with:

```punpun
bring std::io;

fn greet(name: String) -> String {
    return "Hello, " + name;
}

launch {
    say(greet("PunPun"));
}
```

Your normal edit loop is intentionally small:

```sh
pp check
pp run
pp build --release
```

Single-file programs work too:

```sh
pp run examples/showcase.pp
```

## Language at a glance

### Generics + algebraic results

```punpun
fn checked(flag: bool) -> Result<int, str> {
    if flag {
        return Result::Ok(42);
    }
    return Result::Error("not ready");
}

fn use_checked(flag: bool) -> Result<int, str> {
    let value = checked(flag)?;
    return Result::Ok(value + 1);
}

launch {
    say(match use_checked(true) {
        Result::Ok(value) => value,
        Result::Error(message) => 0,
    });
}
```

### Objects and contracts

```punpun
contract Damageable {
    fn damage(amount: i64);
}

object Enemy meets Damageable {
    private let mut health: i64;

    public init(health: i64) {
        self.health = health;
    }

    public fn damage(amount: i64) {
        self.health -= amount;
    }

    public fn hp() -> i64 {
        return self.health;
    }
}

launch {
    let mut enemy = Enemy(100);
    enemy.damage(25);
    say(enemy.hp());
}
```

### Safe references and explicit unsafe code

```punpun
fn bump(value: &mut i64) {
    *value = *value + 1;
}

launch {
    let mut value = 41;
    bump(&mut value);

    unsafe {
        let pointer: *i64 = &raw value;
        *pointer = 43;
    }

    say(value);
}
```

### Optional LLVM backend

PunPun semantics do not change when LLVM is selected:

```sh
ppc build main.pp --llvm-backend
ppc run main.pp --llvm-backend
ppc emit-llvm main.pp
```

Set `PUNPUN_LLVM_CC` to select a particular Clang executable.

## Developer tooling

Useful commands:

```text
pp check [file.pp]              parse + type/ownership check
pp build [file.pp]              compile a native executable
pp run [file.pp]                build and run
pp test [--doc]                 run project tests and optional doctests
pp doc [--check]                generate/check API documentation
pp debug-map [file.pp]          emit deterministic source debug mapping
pp debug [file.pp]              launch GDB/LLDB for a debug build
pp fuzz                         deterministic frontend mutation fuzzing
pp compat                       compare supported backend behavior
pp stress                       repeat structured async/compiler workloads
pp pgo [file.pp]                train a portable-C PGO build
pp fmt [file.pp]                format PunPun source
pp clean                        clear project build cache

pp doctor                       inspect the installed SDK/toolchains
pp explain E0201                explain a compiler diagnostic
pp migrate [file.pp]            migrate common legacy syntax
pp ast | ir | asm               inspect compiler stages
pp emit-c | emit-asm | emit-llvm
pp toolchain detect             probe available native toolchains
pp editor install-vscode        install bundled VS Code support
```

Projects use `Punpun.toml` and deterministic `Punpun.lock` resolution. The compiler, editor, runtime, package manager, sites, and release artifacts derive the product version from the repository [`VERSION`](VERSION).

## PPX packages

PPX is PunPun's package tool. It materializes packages into the same build graph consumed by `pp`.

### Find and install

```sh
ppx search requests
ppx info requests
ppx install requests
ppx tree
```

### Publish your own package

Validate first:

```sh
ppx publish --dry-run
```

Authenticate and publish:

```sh
ppx register developer
ppx login developer
ppx publish
```

`ppx upload` runs the same upload flow. Registry versions are immutable. Package identity/dependency metadata is checked against the uploaded `Punpun.toml`; downloaded archives are SHA-256 verified; new archives also contain a deterministic internal per-file integrity manifest. Use `ppx verify <archive>` for offline verification and `ppx audit --deny-injection` to enforce a no-native-injection dependency policy. Non-loopback registries are HTTPS-by-default.

Download without changing a project:

```sh
ppx download my_package 1.2.0 -o my_package-1.2.0.zip
```

See [`ppx/README.md`](ppx/README.md) and the [PPX publishing lesson](docs-site/content/ppx-publishing.md).

## Documentation

The documentation site is built from `docs-site/content/` and now follows a beginner → intermediate → advanced learning path.

Start with:

1. [Learn PunPun](docs-site/content/index.md)
2. [Getting Started](docs-site/content/getting-started.md)
3. [Language Basics](docs-site/content/language.md)
4. [Functions](docs-site/content/functions.md)
5. [Control Flow](docs-site/content/control-flow.md)
6. [Objects, Structs, and Contracts](docs-site/content/objects.md)
7. [Generics, Option, Result, and Match](docs-site/content/generics-results.md)
8. [Packages and PPX](docs-site/content/packages.md)
9. [Native Memory](docs-site/content/memory.md)

Other useful references:

- [Compiler architecture](docs/compiler-architecture.md)
- [0.6 language specification](spec/0.6/)
- [Editor and file icons](docs-site/content/editor-icons.md)
- [PPX publishing](docs-site/content/ppx-publishing.md)
- [Structured async](docs/language/structured-async.md)
- [PPX security](docs-site/content/ppx-security.md)
- [Generated API reference](docs/api/REFERENCE.md)
- [Release status](PROJECT_STATUS.txt)
- [Roadmap](ROADMAP.md)
- [Release notes](RELEASE_NOTES.md)

Build the static docs locally:

```sh
python3 docs-site/build.py
python3 -m http.server 8000 --directory docs-site/dist
```

## Repository layout

```text
compiler/         parser, semantics, ownership, HIR, MIR, Machine IR, backends
runtime/          native runtime pieces
stdlib/           standard-library source
packages/         first-party PPX packages
ppx/              PPX client
ppx-registry/     reference registry service
editors/vscode/   VS Code extension + bundled language server
packaging/        Linux/Arch desktop + package integration
installers/       platform installer projects
spec/             language/version specifications
docs-site/        learning/reference documentation website
ppx-site/         package catalog website
tests/            language, tooling, release, ecosystem regressions
scripts/          build/release/version/validation automation
selfhost/         PunPun-written bootstrap compiler
```

## Build from source

On Linux with a C/C++ toolchain:

```sh
make compiler
./build/ppc --version
./tests/run.sh
```

Useful qualification checks:

```sh
make selfhost
python3 scripts/check_version.py
python3 scripts/privacy_audit.py .
python3 docs-site/build.py
python3 ppx-site/build.py
python3 scripts/package_vsix.py
python3 scripts/docgen.py --check
python3 scripts/doctest.py --ppc ./build/ppc
python3 scripts/compat_matrix.py --quick
python3 scripts/fuzz_frontend.py --iterations 60 --seed 20560
python3 scripts/stress.py --quick
```

## Contributing

Changes should preserve the basic rule that a feature is not complete merely because the parser recognizes it. Language changes should consider syntax, semantics, ownership/types, IR lowering, code generation, diagnostics, tests, documentation, and editor support where applicable.

Before opening a change:

```sh
make test
```

For package-manager changes also run:

```sh
python3 -m unittest tests.test_ecosystem -v
```

For release/publisher changes run the release-hygiene tests and inspect the generated artifact list rather than checking build junk into source.

## License

PunPun is released under the [MIT License](LICENSE). Third-party dependency and license notes are documented in [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md).
