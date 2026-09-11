# Publishing PunPun

PunPun releases use **build → qualify → promote**. The publisher bundle may update the candidate source on `main`, but it must not promote release downloads or the documentation/PPX sites until the exact candidate commit passes the required Linux, Arch, and Windows qualification workflow.

## One-command publication

On CachyOS/Arch:

```sh
sudo pacman -S --needed git github-cli unzip coreutils
gh auth login
```

Extract `PunPun-<VERSION>-publisher.zip`, enter the extracted folder, then run:

```sh
chmod +x publish-punpun.sh
./publish-punpun.sh
```

The script derives the GitHub account from the authenticated `gh` session and never embeds a token in the bundle.

## Publication sequence

A successful run:

1. verifies every publisher input against `SHA256SUMS`;
2. extracts and sanitizes the clean source archive;
3. replaces the `punpun` source repository with the candidate source while preserving `.git`;
4. creates or verifies the immutable `v<VERSION>` candidate tag at that exact source commit;
5. waits for `platform-release.yml` to qualify that exact commit on Linux, Arch, and Windows;
6. **stops immediately on any failed platform job**;
7. only after a green qualification run, promotes the release assets for the candidate tag;
8. removes stale uploaded assets from that tag only.

The documentation site and the PPX catalog are built and published from their
own repositories (`punpun-docs`, `punpun-ppx`), which hold their own sources.
This script no longer overwrites them.

The old publish-while-CI-runs behavior is intentionally gone. A red qualification run leaves the candidate source/tag available for diagnosis but does not create a falsely healthy release.

## Cleanup policy

Source/publication cleanup removes machine-local caches, compiled host output, and backup/reject files including:

```text
.punpun/  build/  dist/  node_modules/  __pycache__/  .pytest_cache/
.mypy_cache/  .ruff_cache/  .idea/  __MACOSX/  .ppx-registry/  htmlcov/
*.pyc  *.tmp  *.swp  *.swo  *.bak  *.bak-*  *.orig  *.rej  *~
*.o  *.a  *.so  *.dll  *.exe
.DS_Store  Thumbs.db  desktop.ini  .coverage
```

The cleanup is bounded to temporary release/publisher trees and does not follow symlinks. Project source such as `.github/`, `.vscode/`, compiler/runtime/stdlib code, packages, editor support, installers, docs/spec/tests/examples, manifests, lock/reproducibility metadata, and governance files is retained.

## Local release verification

From a clean source checkout:

```sh
python3 scripts/build_brand.py --repo-root .
python3 scripts/check_links.py README.md docs compiler/docs spec examples editors/vscode/README.md CONTRIBUTING.md SECURITY.md
make -j2 test
./selfhost/bootstrap.sh
python3 scripts/release.py
```

Then verify:

```sh
cd "dist/release-$(cat VERSION)"
sha256sum -c SHA256SUMS
```

Linux artifacts are executable-qualified on Linux. Arch installation/upgrade/removal and Windows MSI/file-association/upgrade/uninstall claims become release-qualified only when their real platform jobs pass. Do not manufacture native artifacts that were not built on a suitable host.

## 1.0 stable-release gates

Before promoting 1.x, run `make stability`, `python3 scripts/abi_check.py --ppc ./build/ppc`, and `python3 scripts/platform_policy.py --check`. Release assembly emits source SBOM/provenance and optionally signs when `PUNPUN_RELEASE_SIGNING_KEY` is configured.
