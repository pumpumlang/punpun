# Publish PunPun

The publisher bundle contains sanitized source, Linux binaries, an Arch package, the VS Code extension, deploy-ready websites, Windows installer source, reports and SHA-256 checksums.

## One-command publication

On CachyOS/Arch, install the required tools once:

```sh
sudo pacman -S --needed git github-cli unzip coreutils
gh auth login
```

Open the extracted `PunPun-<VERSION>-publisher` folder in the terminal and run:

```sh
chmod +x publish-punpun.sh
./publish-punpun.sh
```

Even when Fish is your interactive shell, run this file exactly as shown; its Bash interpreter is selected automatically.

The script safely:

1. derives the account from your authenticated `gh` session;
2. verifies every bundled checksum;
3. creates or updates the `punpun` source repository;
4. creates or updates the release tag derived from the bundled `VERSION` file;
5. creates or updates the `punpun-docs` GitHub Pages site;
6. creates or updates the `punpun-ppx` GitHub Pages site;
7. prints the exact URLs at the end.

It stages repository updates in a temporary directory, leaves your extracted bundle unchanged and stores no access token or account name in project files.

Expected public URLs:

```text
https://YOUR_ACCOUNT.github.io/punpun-docs/
https://YOUR_ACCOUNT.github.io/punpun-ppx/
```

Do not use `/docs-site/`; that is a source-directory name, not the published repository name. GitHub Pages can take a minute or two to replace an earlier 404 page after its first deployment.

## If the script is outside the publisher folder

The script automatically checks the current directory, its own directory, Desktop and Downloads. You can also provide the folder explicitly:

```sh
env PUNPUN_PUBLISHER_DIR="$HOME/Desktop/PunPun-<VERSION>-publisher" bash publish-punpun.sh
```

## Local verification from source

```sh
make clean all
./tests/run.sh
make selfhost
python3 scripts/release.py
python3 -m http.server 8000 --directory docs-site/dist
```

Open <http://localhost:8000> and stop the server with `Ctrl+C`.

## Optional Cloudflare Pages mirror

From the extracted publisher folder:

```sh
rm -rf /tmp/punpun-docs-publish /tmp/punpun-ppx-publish
mkdir -p /tmp/punpun-docs-publish /tmp/punpun-ppx-publish
version=$(tr -d '\r\n' < VERSION)
unzip "$PWD/websites/PunPun-${version}-docs-site.zip" -d /tmp/punpun-docs-publish
unzip "$PWD/websites/PunPun-${version}-ppx-site.zip" -d /tmp/punpun-ppx-publish
npx wrangler@latest login
npx wrangler@latest pages deploy /tmp/punpun-docs-publish --project-name punpun-docs
npx wrangler@latest pages deploy /tmp/punpun-ppx-publish --project-name punpun-ppx
```

The public PPX site uses its built-in package catalog. A local registry is no longer required for the website to render and search first-party packages.

## Release honesty

Linux and Arch payloads are real build artifacts. The bundle contains Windows WiX installer **source** until the Windows CI job successfully builds and validates the MSI and setup EXE. Never rename an archive to imitate a native installer.

Do not publish `.punpun/`, `build/`, `__pycache__/`, local databases, shell history, environment files, access tokens or private signing keys.
