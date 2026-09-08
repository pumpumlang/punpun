# Publishing PunPun

The publisher bundle contains only the files required to update the PunPun source repository, release assets, documentation site, and PPX catalog. It is assembled from fresh release outputs rather than copied from an old publisher directory.

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

The script derives the GitHub account from the authenticated `gh` session. It does not hardcode a personal account name or token.

## What the script updates

A successful run:

1. verifies the publisher bundle against `SHA256SUMS`;
2. extracts and sanitizes the source archive in a temporary directory;
3. replaces the contents of the `punpun` source repository, preserving only its `.git` database;
4. triggers the platform release workflow when GitHub accepts the dispatch;
5. updates the current `v<VERSION>` prerelease and uploads the exact current asset set;
6. removes stale uploaded assets from **that release tag only** when they are no longer present in the publisher bundle;
7. replaces the `punpun-docs` Pages repository with the newly built documentation site;
8. replaces the `punpun-ppx` Pages repository with the newly built PPX site;
9. enables/refreshes GitHub Pages and prints the resulting repository/site URLs.

Because repository contents are replaced rather than overlaid, a file removed from the current source/site build is also removed from the published repository on the next run.

## Cleanup policy

Cleanup is deliberately narrow. The publisher removes known generated, machine-local, cache, and compiled-host artifacts. It does **not** use a broad extension allowlist that might silently delete legitimate source.

### Removed directories

```text
.punpun/
build/
dist/                  source publication only
__pycache__/
.pytest_cache/
.mypy_cache/
.ruff_cache/
node_modules/
.idea/
__MACOSX/
.ppx-registry/
htmlcov/
```

### Removed files

```text
*.pyc
*.tmp
*.swp
*.swo
*~
.DS_Store
Thumbs.db
desktop.ini
.coverage
```

For **source publication only**, host-compiled files are also removed:

```text
*.o
*.a
*.so
*.dll
*.exe
```

These are rebuilt by the project/release toolchain and should never make the public source tree depend on the machine that assembled the bundle.

### Intentionally retained

The cleanup pass keeps project content that may look “developer-ish” but is part of the product/repository:

```text
.github/
.vscode/
compiler/
runtime source
stdlib/
packages/
ppx/
ppx-registry/
editors/
packaging/
installers/
docs + docs-site source
spec/
tests/
examples/
scripts/
LICENSE
README / roadmap / release documentation
manifest and reproducibility metadata
```

The temporary cleanup functions use `find -P` and operate only inside the publisher's temporary extraction directories. They do not follow symlinks and do not clean the user's working repository.

## Release asset pruning safety

When a release tag already exists, `publish-punpun.sh` compares its uploaded asset names with the current publisher bundle. An old uploaded asset is deleted only when:

- it belongs to the exact current tag `v<VERSION>`;
- it is an uploaded release asset returned by GitHub for that tag; and
- its filename is absent from the current publisher bundle's release asset list.

GitHub-generated source archives are not touched. Other tags/releases are not touched.

## If the script is outside the publisher folder

The script checks its own directory, the current directory, Desktop, and Downloads. You can set the bundle explicitly:

```sh
env PUNPUN_PUBLISHER_DIR="$HOME/Desktop/PunPun-<VERSION>-publisher" bash publish-punpun.sh
```

## Local release verification

From source:

```sh
make clean all
./tests/run.sh
make selfhost
python3 scripts/release.py
```

Build/preview the sites separately:

```sh
python3 docs-site/build.py
python3 ppx-site/build.py
python3 -m http.server 8000 --directory docs-site/dist
```

## Optional Cloudflare Pages mirror

The publisher's website ZIPs are deployment-ready static output. Extract those ZIPs to temporary directories before passing them to another hosting provider; do not publish the source-site folders as though they were generated output.

## Release honesty

Linux artifacts are built and validated on the Linux host. Windows WiX installer source is published until Windows CI/VM qualification produces real MSI/Setup artifacts. Never rename an archive to impersonate a native installer.
