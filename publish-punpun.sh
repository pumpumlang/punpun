#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
VERSION=${PUNPUN_VERSION:-}
if [[ -z "$VERSION" && -f "$SCRIPT_DIR/VERSION" ]]; then
    VERSION=$(tr -d '\r\n' < "$SCRIPT_DIR/VERSION")
fi
[[ -n "$VERSION" ]] || { printf 'publish-punpun: VERSION is missing beside this script\n' >&2; exit 1; }
TAG="v${VERSION}"
PUBLISHER_NAME="PunPun-${VERSION}-publisher"
DOCS_REPO="punpun-docs"
PPX_REPO="punpun-ppx"
SOURCE_REPO="punpun"
WORKFLOW="platform-release.yml"

green='\033[1;32m'
blue='\033[1;36m'
yellow='\033[1;33m'
reset='\033[0m'

step() { printf '\n%b==>%b %s\n' "$blue" "$reset" "$*"; }
ok() { printf '%b✓%b %s\n' "$green" "$reset" "$*"; }
warn() { printf '%b!%b %s\n' "$yellow" "$reset" "$*" >&2; }
die() { printf 'publish-punpun: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing '$1' (install it, then rerun this script)"; }

print_cleanup_policy() {
    cat <<'EOF'
Publish cleanup policy:
  stale repository files: removed because each publish replaces the remote checkout
  generated/cache dirs:   .punpun build dist __pycache__ .pytest_cache .mypy_cache .ruff_cache node_modules .idea __MACOSX .ppx-registry htmlcov
  generated files:        *.pyc *.tmp *.swp *.swo *.bak *.bak-* *.orig *.rej *.o *.a *.so *.dll *.exe *~ .DS_Store Thumbs.db desktop.ini .coverage
  release assets:          uploaded assets absent from this publisher bundle are removed from this release tag only
  intentionally retained: .github .vscode docs/spec/tests/source files, LICENSE, manifests and lock/reproducibility metadata
EOF
}

sanitize_publish_tree() {
    local tree=$1 mode=${2:-source}
    [[ -d "$tree" ]] || die "cleanup target is not a directory: $tree"

    # Never follow symlinks and never delete outside the temporary publish tree.
    find -P "$tree" -depth \
        \( -type d \( -name .punpun -o -name build -o -name __pycache__ -o -name .pytest_cache -o -name .mypy_cache -o -name .ruff_cache -o -name node_modules -o -name .idea -o -name __MACOSX -o -name .ppx-registry -o -name htmlcov \) \
        -o -type f \( -name '*.pyc' -o -name '*.tmp' -o -name '*.swp' -o -name '*.swo' -o -name '*.bak' -o -name '*.bak-*' -o -name '*.orig' -o -name '*.rej' -o -name '*~' -o -name .DS_Store -o -name Thumbs.db -o -name desktop.ini -o -name .coverage \) \) \
        -exec rm -rf -- {} +

    if [[ "$mode" == source ]]; then
        find -P "$tree" -type f \
            \( -name '*.o' -o -name '*.a' -o -name '*.so' -o -name '*.dll' -o -name '*.exe' \) \
            -delete
        find -P "$tree" -depth -type d -name dist -exec rm -rf -- {} +
    fi
}

prune_release_assets() {
    local repo=$1 tag=$2
    shift 2
    local desired_name current keep candidate
    declare -A desired=()
    for candidate in "$@"; do
        desired_name=$(basename -- "$candidate")
        desired["$desired_name"]=1
    done
    while IFS= read -r current; do
        [[ -n "$current" ]] || continue
        keep=${desired["$current"]:-}
        if [[ -z "$keep" ]]; then
            gh release delete-asset "$tag" "$current" --repo "$repo" -y >/dev/null
            ok "removed stale release asset $current"
        fi
    done < <(gh release view "$tag" --repo "$repo" --json assets --jq '.assets[].name' 2>/dev/null || true)
}

find_publisher() {
    local candidate old_bundle=''
    for candidate in \
        "${PUNPUN_PUBLISHER_DIR:-}" \
        "$SCRIPT_DIR" \
        "$PWD" \
        "$HOME/Desktop/$PUBLISHER_NAME" \
        "$HOME/Downloads/$PUBLISHER_NAME"
    do
        [[ -n "$candidate" ]] || continue
        if [[ -f "$candidate/SHA256SUMS" && -d "$candidate/source" && -d "$candidate/websites" ]]; then
            if unzip -Z1 "$candidate/websites/PunPun-${VERSION}-ppx-site.zip" 2>/dev/null | grep -qx 'static/catalog.json'; then
                CDPATH= cd -- "$candidate" && pwd
                return
            fi
            old_bundle=$candidate
        fi
    done
    [[ -z "$old_bundle" ]] || die "found an older publisher bundle at $old_bundle. Extract the current publisher ZIP into a fresh folder."
    die "cannot find $PUBLISHER_NAME. Put this script inside that folder, or set PUNPUN_PUBLISHER_DIR."
}

git_identity() {
    local repository=$1
    git -C "$repository" config user.name "PunPun Project"
    git -C "$repository" config user.email "punpun-project""@""users.noreply.github.com"
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

    [[ "$pages" != yes ]] || : > "$checkout/.nojekyll"
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

ensure_candidate_tag() {
    local checkout=$1 sha=$2 remote_sha=''
    remote_sha=$(gh api "repos/$GH_ACCOUNT/$SOURCE_REPO/commits/$TAG" --jq .sha 2>/dev/null || true)
    if [[ -n "$remote_sha" ]]; then
        [[ "$remote_sha" == "$sha" ]] || die "$TAG already points at $remote_sha, not candidate $sha; release tags are immutable"
        ok "$TAG already identifies the exact candidate commit"
        return
    fi
    git -C "$checkout" tag "$TAG" "$sha"
    git -C "$checkout" push origin "refs/tags/$TAG" >/dev/null
    ok "created immutable candidate tag $TAG at $sha"
}

wait_for_platform_qualification() {
    local sha=$1 run_id='' attempts=0 dispatched=0
    step "Waiting for Linux, Arch and Windows qualification"
    while [[ -z "$run_id" ]]; do
        run_id=$(gh run list --repo "$GH_ACCOUNT/$SOURCE_REPO" --workflow "$WORKFLOW" --limit 30 \
            --json databaseId,headSha --jq ".[] | select(.headSha == \"$sha\") | .databaseId" 2>/dev/null | head -n1 || true)
        [[ -n "$run_id" ]] && break
        attempts=$((attempts + 1))
        if (( attempts == 4 && dispatched == 0 )); then
            gh workflow run "$WORKFLOW" --repo "$GH_ACCOUNT/$SOURCE_REPO" --ref "$TAG" >/dev/null || die "could not start $WORKFLOW"
            dispatched=1
            ok "explicitly dispatched $WORKFLOW for $TAG"
        fi
        (( attempts < 30 )) || die "no platform qualification run appeared for candidate $sha"
        sleep 5
    done
    printf 'Qualification run: %s\n' "$run_id"
    if ! gh run watch "$run_id" --repo "$GH_ACCOUNT/$SOURCE_REPO" --exit-status; then
        die "platform qualification failed; release downloads and sites were NOT promoted"
    fi
    QUALIFICATION_RUN_ID="$run_id"
    ok "exact candidate $sha passed platform qualification"
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
SCRIPT_PATH="$SCRIPT_DIR/$(basename -- "${BASH_SOURCE[0]}")"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/punpun-publish.XXXXXX")
trap 'rm -rf -- "$WORK"' EXIT HUP INT TERM

gh auth status >/dev/null 2>&1 || die "GitHub CLI is not logged in; run 'gh auth login' first"
GH_ACCOUNT=$(gh api user --jq .login)
printf '%bPunPun %s publisher%b\n' "$green" "$VERSION" "$reset"
printf 'Account:   %s\nBundle:    %s\n' "$GH_ACCOUNT" "$ROOT"
print_cleanup_policy

step "Verifying every release file"
(cd "$ROOT" && sha256sum -c SHA256SUMS)
ok "release checksums passed"

step "Publishing the candidate source repository"
mkdir -p "$WORK/source"
unzip -q "$ROOT/source/PunPun-${VERSION}-source.zip" -d "$WORK/source"
SOURCE_TREE="$WORK/source/PunPun-${VERSION}-source"
[[ -d "$SOURCE_TREE" ]] || die "source archive has an unexpected layout"
sanitize_publish_tree "$SOURCE_TREE" source
sync_repository "$SOURCE_REPO" "$SOURCE_TREE" "Publish PunPun $VERSION candidate"
SOURCE_CHECKOUT="$WORK/repos/$SOURCE_REPO"
SOURCE_SHA=$(git -C "$SOURCE_CHECKOUT" rev-parse HEAD)
ensure_candidate_tag "$SOURCE_CHECKOUT" "$SOURCE_SHA"
wait_for_platform_qualification "$SOURCE_SHA"

# Nothing below this line runs unless the exact tagged candidate is qualified.
step "Collecting exact qualified platform artifacts"
QUALIFIED="$WORK/qualified"
PROMOTE="$WORK/promote"
mkdir -p "$QUALIFIED" "$PROMOTE"
gh run download "$QUALIFICATION_RUN_ID" --repo "$GH_ACCOUNT/$SOURCE_REPO" --dir "$QUALIFIED"

# Start from the deterministic publisher artifacts, then let the exact green CI
# run override host-specific files with the artifacts it actually qualified.
for candidate in "$ROOT"/linux/* "$ROOT"/arch/* "$ROOT"/editor/* "$ROOT"/windows/* "$ROOT"/websites/*.zip "$ROOT"/reports/* "$ROOT/SHA256SUMS"; do
    [[ -f "$candidate" ]] || continue
    cp -f "$candidate" "$PROMOTE/$(basename -- "$candidate")"
done
while IFS= read -r -d '' candidate; do
    case "$(basename -- "$candidate")" in
        *.msi|*.exe|*.run|*.zip|*.zst|*.vsix|PKGBUILD|RELEASE_VALIDATION.md|release-manifest.json|SOURCE_SBOM.spdx.json|RELEASE_PROVENANCE.json|RELEASE_SIGNATURES.json|SHA256SUMS)
            cp -f "$candidate" "$PROMOTE/$(basename -- "$candidate")" ;;
    esac
done < <(find "$QUALIFIED" -type f -print0)
PUBLISH_SCRIPT="$ROOT/publish-punpun.sh"
[[ -f "$PUBLISH_SCRIPT" ]] || PUBLISH_SCRIPT="$SCRIPT_PATH"
cp -f "$PUBLISH_SCRIPT" "$PROMOTE/publish-punpun.sh"
mapfile -d '' assets < <(find "$PROMOTE" -maxdepth 1 -type f -print0 | sort -z)
(( ${#assets[@]} > 1 )) || die "release assets are missing"

step "Promoting stable release downloads"
release_args=(--repo "$GH_ACCOUNT/$SOURCE_REPO" --title "PunPun $VERSION" --notes-file "$ROOT/RELEASE_NOTES.md")
# A clean X.Y.Z is stable. Pre-release identifiers remain prereleases.
if [[ "$VERSION" == *-* ]]; then release_args+=(--prerelease); fi
if gh release view "$TAG" --repo "$GH_ACCOUNT/$SOURCE_REPO" >/dev/null 2>&1; then
    prune_release_assets "$GH_ACCOUNT/$SOURCE_REPO" "$TAG" "${assets[@]}"
    gh release edit "$TAG" "${release_args[@]}" >/dev/null
    gh release upload "$TAG" "${assets[@]}" --repo "$GH_ACCOUNT/$SOURCE_REPO" --clobber
    ok "updated qualified release $TAG"
else
    gh release create "$TAG" "${assets[@]}" "${release_args[@]}" --verify-tag
    ok "created qualified release $TAG"
fi

step "Publishing the documentation website"
mkdir -p "$WORK/docs"
unzip -q "$ROOT/websites/PunPun-${VERSION}-docs-site.zip" -d "$WORK/docs"
sanitize_publish_tree "$WORK/docs" website
sync_repository "$DOCS_REPO" "$WORK/docs" "Publish PunPun $VERSION documentation" yes
enable_pages "$DOCS_REPO"

step "Publishing the PPX package website"
mkdir -p "$WORK/ppx"
unzip -q "$ROOT/websites/PunPun-${VERSION}-ppx-site.zip" -d "$WORK/ppx"
sanitize_publish_tree "$WORK/ppx" website
sync_repository "$PPX_REPO" "$WORK/ppx" "Publish PunPunXPac $VERSION catalog" yes
enable_pages "$PPX_REPO"

printf '\n%bQualified PunPun release published.%b\n' "$green" "$reset"
printf 'Source:        https://github.com/%s/%s\n' "$GH_ACCOUNT" "$SOURCE_REPO"
printf 'Release:       https://github.com/%s/%s/releases/tag/%s\n' "$GH_ACCOUNT" "$SOURCE_REPO" "$TAG"
printf 'Documentation: https://%s.github.io/%s/\n' "$GH_ACCOUNT" "$DOCS_REPO"
printf 'PPX catalog:   https://%s.github.io/%s/\n' "$GH_ACCOUNT" "$PPX_REPO"
