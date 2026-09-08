# Publishing PunPun from CachyOS/Arch Linux

The publisher bundle contains sanitized source, Linux binaries, the installer,
Arch packaging, deploy-ready websites, the VS Code extension, Windows installer
source, validation reports, and SHA-256 checksums. It contains no account name,
email address, home-directory path, access token, or signing key.

## 1. Install publishing tools

```sh
sudo pacman -S --needed base-devel git github-cli python nodejs npm unzip zstd
gh auth login
export GH_ACCOUNT="$(gh api user --jq .login)"
```

`GH_ACCOUNT` is obtained from the account you authenticate with; no username is
hardcoded in the project.

## 2. Verify and unpack

From the directory containing the publisher ZIP:

```sh
unzip PunPun-0.5.0-beta-publisher.zip
cd PunPun-0.5.0-beta-publisher
sha256sum -c SHA256SUMS
```

## 3. Publish the source repository

```sh
mkdir -p /tmp/punpun-publish-source
unzip source/PunPun-0.5.0-beta-source.zip -d /tmp/punpun-publish-source
cd /tmp/punpun-publish-source/PunPun-0.5.0-beta-source
git init -b main
git add .
git commit -m "PunPun 0.5.0-beta"
gh repo create "$GH_ACCOUNT/punpun" --public --source=. --remote=origin --push
```

If the repository already exists, use its normal clone and copy the new source
into it instead of creating it again.

## 4. Create the downloadable GitHub release

Return to the unpacked publisher directory, then run:

```sh
gh release create v0.5.0-beta \
  linux/* arch/* editor/* windows/* websites/*.zip reports/* SHA256SUMS \
  --repo "$GH_ACCOUNT/punpun" \
  --title "PunPun 0.5.0-beta" \
  --notes-file RELEASE_NOTES.md \
  --prerelease
```

This uploads real Linux artifacts and the Windows installer **source**. It does
not claim that a Windows MSI/EXE was built on Linux.

## 5. Publish documentation with GitHub Pages

```sh
rm -rf /tmp/punpun-docs-publish
mkdir -p /tmp/punpun-docs-publish
unzip websites/PunPun-0.5.0-beta-docs-site.zip -d /tmp/punpun-docs-publish
cd /tmp/punpun-docs-publish
touch .nojekyll
git init -b main
git add .
git commit -m "Publish PunPun documentation"
gh repo create "$GH_ACCOUNT/punpun-docs" --public --source=. --remote=origin --push
gh api --method POST "repos/$GH_ACCOUNT/punpun-docs/pages" \
  -f 'source[branch]=main' -f 'source[path]=/'
```

The documentation URL will be:

```text
https://YOUR_GITHUB_USERNAME.github.io/punpun-docs/
```

For later documentation updates, replace the files in the repository, commit,
and push. Do not run `gh repo create` again.

## 6. Publish the PPX website

```sh
rm -rf /tmp/punpun-ppx-publish
mkdir -p /tmp/punpun-ppx-publish
unzip websites/PunPun-0.5.0-beta-ppx-site.zip -d /tmp/punpun-ppx-publish
cd /tmp/punpun-ppx-publish
touch .nojekyll
git init -b main
git add .
git commit -m "Publish PunPunXPac website"
gh repo create "$GH_ACCOUNT/punpun-ppx" --public --source=. --remote=origin --push
gh api --method POST "repos/$GH_ACCOUNT/punpun-ppx/pages" \
  -f 'source[branch]=main' -f 'source[path]=/'
```

The PPX frontend needs a separately hosted registry API. Until that backend is
deployed, it will honestly display that the registry is unavailable. Never put
registry tokens in the website repository or browser JavaScript.

## Optional: Cloudflare Pages instead

After `npm` is installed and you have logged into Cloudflare:

```sh
npx wrangler@latest login
npx wrangler@latest pages deploy /tmp/punpun-docs-publish --project-name punpun-docs
npx wrangler@latest pages deploy /tmp/punpun-ppx-publish --project-name punpun-ppx
```

## Local verification before publishing

From the source tree:

```sh
make clean all
./tests/run.sh
make selfhost
python3 scripts/release.py
python3 -m http.server 8000 --directory docs-site/dist
```

Open `http://localhost:8000`. Stop the server with `Ctrl+C`.

Do not publish `.punpun/`, `build/`, `__pycache__/`, local databases, shell
history, environment files, access tokens, or private signing keys.
