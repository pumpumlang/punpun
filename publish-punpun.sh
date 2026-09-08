#!/usr/bin/env bash
set -Eeuo pipefail

VERSION="0.5.0-beta"
TAG="v${VERSION}"
PUBLISHER_NAME="PunPun-${VERSION}-publisher"
DOCS_REPO="punpun-docs"
PPX_REPO="punpun-ppx"
SOURCE_REPO="punpun"

green='\033[1;32m'
blue='\033[1;36m'
yellow='\033[1;33m'
reset='\033[0m'

step() { printf '\n%b==>%b %s\n' "$blue" "$reset" "$*"; }
ok() { printf '%b✓%b %s\n' "$green" "$reset" "$*"; }
warn() { printf '%b!%b %s\n' "$yellow" "$reset" "$*" >&2; }
die() { printf 'publish-punpun: %s\n' "$*" >&2; exit 1; }

need() {
    command -v "$1" >/dev/null 2>&1 || die "missing '$1' (install it, then rerun this script)"
}

find_publisher() {
    local script_dir candidate
    script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
    for candidate in \
        "${PUNPUN_PUBLISHER_DIR:-}" \
        "$script_dir" \
        "$PWD" \
        "$HOME/Desktop/$PUBLISHER_NAME" \
        "$HOME/Downloads/$PUBLISHER_NAME"
    do
        [[ -n "$candidate" ]] || continue
        if [[ -f "$candidate/SHA256SUMS" && -d "$candidate/source" && -d "$candidate/websites" ]]; then
            CDPATH= cd -- "$candidate" && pwd
            return
        fi
    done
    die "cannot find $PUBLISHER_NAME. Put this script inside that folder, or set PUNPUN_PUBLISHER_DIR."
}

git_identity() {
    local repository=$1
    git -C "$repository" config user.name "$GH_NAME"
    git -C "$repository" config user.email "${GH_ACCOUNT}@users.noreply.github.com"
}

replace_checkout_contents() {
    local checkout=$1 source=$2
    find "$checkout" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf -- {} +
    cp -a "$source"/. "$checkout"/
}

sync_repository() {
    local repo_name=$1 source=$2 message=$3 pages=${4:-no}
    local checkout="$WORK/repos/$repo_name"
    mkdir -p "$(dirname "$checkout")"

    if gh repo view "$GH_ACCOUNT/$repo_name" >/dev/null 2>&1; then
        gh repo clone "$GH_ACCOUNT/$repo_name" "$checkout" -- --depth 1 >/dev/null
        replace_checkout_contents "$checkout" "$source"
    else
        mkdir -p "$checkout"
        cp -a "$source"/. "$checkout"/
        git -C "$checkout" init -b main >/dev/null
    fi

    if [[ "$pages" == yes ]]; then
        : > "$checkout/.nojekyll"
    fi

    git_identity "$checkout"
    git -C "$checkout" add -A
    if git -C "$checkout" diff --cached --quiet; then
        ok "$repo_name is already current"
    else
        git -C "$checkout" commit -m "$message" >/dev/null
        git -C "$checkout" branch -M main
        if git -C "$checkout" remote get-url origin >/dev/null 2>&1; then
            git -C "$checkout" push origin HEAD:main >/dev/null
        else
            gh repo create "$GH_ACCOUNT/$repo_name" --public --source="$checkout" --remote=origin --push >/dev/null
        fi
        ok "updated $GH_ACCOUNT/$repo_name"
    fi
}

enable_pages() {
    local repo_name=$1
    if gh api "repos/$GH_ACCOUNT/$repo_name/pages" >/dev/null 2>&1; then
        gh api --method PUT "repos/$GH_ACCOUNT/$repo_name/pages" \
            -f 'source[branch]=main' -f 'source[path]=/' >/dev/null
    else
        gh api --method POST "repos/$GH_ACCOUNT/$repo_name/pages" \
            -f 'source[branch]=main' -f 'source[path]=/' >/dev/null
    fi
    gh api --method POST "repos/$GH_ACCOUNT/$repo_name/pages/builds" >/dev/null 2>&1 || true
    ok "GitHub Pages enabled for $repo_name"
}

need bash
need git
need gh
need unzip
need sha256sum

ROOT=$(find_publisher)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/punpun-publish.XXXXXX")
trap 'rm -rf -- "$WORK"' EXIT HUP INT TERM

gh auth status >/dev/null 2>&1 || die "GitHub CLI is not logged in; run 'gh auth login' first"
GH_ACCOUNT=$(gh api user --jq .login)
GH_NAME=$(gh api user --jq '.name // .login')

printf '%bPunPun %s publisher%b\n' "$green" "$VERSION" "$reset"
printf 'Account:   %s\nBundle:    %s\n' "$GH_ACCOUNT" "$ROOT"

step "Verifying every release file"
(cd "$ROOT" && sha256sum -c SHA256SUMS)
ok "release checksums passed"

step "Publishing the complete source repository"
mkdir -p "$WORK/source"
unzip -q "$ROOT/source/PunPun-${VERSION}-source.zip" -d "$WORK/source"
SOURCE_TREE="$WORK/source/PunPun-${VERSION}-source"
[[ -d "$SOURCE_TREE" ]] || die "source archive has an unexpected layout"
sync_repository "$SOURCE_REPO" "$SOURCE_TREE" "Publish PunPun $VERSION"

if gh workflow run platform-release.yml --repo "$GH_ACCOUNT/$SOURCE_REPO" --ref main >/dev/null 2>&1; then
    ok "started fresh Linux, Arch and Windows CI validation"
else
    warn "source was published, but CI could not be started automatically; open the repository Actions tab"
fi

step "Publishing release downloads"
shopt -s nullglob
assets=(
    "$ROOT"/linux/*
    "$ROOT"/arch/*
    "$ROOT"/editor/*
    "$ROOT"/windows/*
    "$ROOT"/websites/*.zip
    "$ROOT"/reports/*
    "$ROOT/SHA256SUMS"
    "$ROOT/publish-punpun.sh"
)
(( ${#assets[@]} > 1 )) || die "release assets are missing"
if gh release view "$TAG" --repo "$GH_ACCOUNT/$SOURCE_REPO" >/dev/null 2>&1; then
    gh release edit "$TAG" --repo "$GH_ACCOUNT/$SOURCE_REPO" \
        --title "PunPun $VERSION" --notes-file "$ROOT/RELEASE_NOTES.md" --prerelease >/dev/null
    gh release upload "$TAG" "${assets[@]}" --repo "$GH_ACCOUNT/$SOURCE_REPO" --clobber
    ok "updated release $TAG"
else
    gh release create "$TAG" "${assets[@]}" --repo "$GH_ACCOUNT/$SOURCE_REPO" \
        --title "PunPun $VERSION" --notes-file "$ROOT/RELEASE_NOTES.md" --prerelease
    ok "created release $TAG"
fi

step "Publishing the documentation website"
mkdir -p "$WORK/docs"
unzip -q "$ROOT/websites/PunPun-${VERSION}-docs-site.zip" -d "$WORK/docs"
sync_repository "$DOCS_REPO" "$WORK/docs" "Publish PunPun $VERSION documentation" yes
enable_pages "$DOCS_REPO"

step "Publishing the PPX package website"
mkdir -p "$WORK/ppx"
unzip -q "$ROOT/websites/PunPun-${VERSION}-ppx-site.zip" -d "$WORK/ppx"
sync_repository "$PPX_REPO" "$WORK/ppx" "Publish PunPunXPac $VERSION catalog" yes
enable_pages "$PPX_REPO"

printf '\n%bEverything is published.%b\n' "$green" "$reset"
printf 'Source:        https://github.com/%s/%s\n' "$GH_ACCOUNT" "$SOURCE_REPO"
printf 'Release:       https://github.com/%s/%s/releases/tag/%s\n' "$GH_ACCOUNT" "$SOURCE_REPO" "$TAG"
printf 'Documentation: https://%s.github.io/%s/\n' "$GH_ACCOUNT" "$DOCS_REPO"
printf 'PPX catalog:   https://%s.github.io/%s/\n' "$GH_ACCOUNT" "$PPX_REPO"
printf '\nGitHub Pages can take a minute or two to replace an earlier 404 page.\n'
