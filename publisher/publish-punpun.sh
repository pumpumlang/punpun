#!/usr/bin/env bash
#
# Drop-in replacement for the repo-root publish-punpun.sh.
# See publisher/FIXES.md for what changed and why.
#
set -Eeuo pipefail

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SCRIPT_PATH="$SCRIPT_DIR/$(basename -- "${BASH_SOURCE[0]}")"

VERSION=${PUNPUN_VERSION:-}
if [[ -z "$VERSION" && -f "$SCRIPT_DIR/VERSION" ]]; then
  # FIX 14: strip all whitespace, not just CR/LF -- a trailing space in VERSION
  # used to produce a tag like "v0.6.0-beta " that never matched anything.
  VERSION=$(tr -d '[:space:]' < "$SCRIPT_DIR/VERSION")
fi
[[ -n "$VERSION" ]] || { printf 'publish-punpun: VERSION is missing beside this script\n' >&2; exit 1; }

TAG="v${VERSION}"
PUBLISHER_NAME="PunPun-${VERSION}-publisher"
DOCS_REPO="punpun-docs"
PPX_REPO="punpun-ppx"
SOURCE_REPO="punpun"

green='\033[1;32m'; blue='\033[1;36m'; yellow='\033[1;33m'; reset='\033[0m'
step() { printf '\n%b==>%b %s\n' "$blue" "$reset" "$*"; }
ok()   { printf '%b✓%b %s\n' "$green" "$reset" "$*"; }
warn() { printf '%b!%b %s\n' "$yellow" "$reset" "$*" >&2; }
die()  { printf 'publish-punpun: %s\n' "$*" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || die "missing '$1' (install it, then rerun this script)"; }

# FIX 7: macOS ships shasum, not sha256sum. Pick whichever exists rather than
# hard-failing on a machine that can perfectly well verify the bundle.
SHA_CHECK=()
pick_sha_tool() {
  if command -v sha256sum >/dev/null 2>&1; then
    SHA_CHECK=(sha256sum -c)
  elif command -v shasum >/dev/null 2>&1; then
    SHA_CHECK=(shasum -a 256 -c)
  else
    die "need sha256sum or shasum to verify the bundle"
  fi
}

# Paths that must survive cleanup even though they look like build junk.
# FIX 8: the old sweep deleted *.so/*.a/*.o/*.exe everywhere, which silently
# ate binary fixtures under tests/ that the release checks depend on.
PROTECTED_PREFIXES=(tests/ spec/ examples/ stdlib/ .github/)

print_cleanup_policy() {
  cat <<'EOF'
Publish cleanup policy:
  stale repository files : removed because each publish replaces the remote checkout
  generated/cache dirs   : .punpun build dist __pycache__ .pytest_cache .mypy_cache
                           .ruff_cache node_modules .idea __MACOSX .ppx-registry htmlcov
  generated files        : *.pyc *.tmp *.swp *.swo *.o *.a *.so *.dll *.exe *~
                           .DS_Store Thumbs.db desktop.ini .coverage
  never removed          : tests/ spec/ examples/ stdlib/ .github/ .vscode/
                           docs/ LICENSE VERSION and lock/reproducibility metadata
  release assets         : uploaded assets absent from this bundle are removed
                           from this release tag only
EOF
}

is_protected() {
  local rel=$1 prefix
  for prefix in "${PROTECTED_PREFIXES[@]}"; do
    [[ $rel == "$prefix"* ]] && return 0
  done
  return 1
}

sanitize_publish_tree() {
  local tree=$1 mode=${2:-source} path rel
  [[ -d "$tree" ]] || die "cleanup target is not a directory: $tree"

  # Never follow symlinks and never delete outside the temporary publish tree.
  find -P "$tree" -depth \
    \( -type d \( -name .punpun -o -name build -o -name __pycache__ -o -name .pytest_cache \
                  -o -name .mypy_cache -o -name .ruff_cache -o -name node_modules -o -name .idea \
                  -o -name __MACOSX -o -name .ppx-registry -o -name htmlcov \) \
    -o -type f \( -name '*.pyc' -o -name '*.tmp' -o -name '*.swp' -o -name '*.swo' -o -name '*~' \
                  -o -name .DS_Store -o -name Thumbs.db -o -name desktop.ini -o -name .coverage \) \) \
    -exec rm -rf -- {} +

  if [[ "$mode" == source ]]; then
    # Source publication must never inherit compiled host artifacts, but it must
    # also not eat checked-in fixtures. Filter through the protected list.
    while IFS= read -r -d '' path; do
      rel=${path#"$tree"/}
      is_protected "$rel" && continue
      rm -f -- "$path"
    done < <(find -P "$tree" -type f \
               \( -name '*.o' -o -name '*.a' -o -name '*.so' -o -name '*.dll' -o -name '*.exe' \) \
               -print0)

    while IFS= read -r -d '' path; do
      rel=${path#"$tree"/}
      is_protected "$rel" && continue
      rm -rf -- "$path"
    done < <(find -P "$tree" -depth -type d -name dist -print0)
  fi
}

prune_release_assets() {
  local repo=$1 tag=$2
  shift 2
  # FIX 2: was `declare -A`, which is global. Repeated calls leaked the previous
  # call's key set, so a second release could keep assets it should have pruned.
  local -A desired=()
  local candidate current
  for candidate in "$@"; do
    desired["$(basename -- "$candidate")"]=1
  done

  while IFS= read -r current; do
    [[ -n "$current" ]] || continue
    if [[ ! -v desired["$current"] ]]; then
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
    # FIX 11: RELEASE_NOTES.md is used later by `gh release edit --notes-file`.
    # It was never part of the bundle test, so a bundle missing it was accepted
    # and then blew up halfway through publishing, after the source push.
    if [[ -f "$candidate/SHA256SUMS" && -f "$candidate/RELEASE_NOTES.md" \
          && -d "$candidate/source" && -d "$candidate/websites" ]]; then
      if unzip -Z1 "$candidate/websites/PunPun-${VERSION}-ppx-site.zip" 2>/dev/null \
           | grep -qx 'static/catalog.json'; then
        CDPATH= cd -- "$candidate" && pwd
        return
      fi
      old_bundle=$candidate
    fi
  done

  [[ -n "$old_bundle" ]] && die "found an older publisher bundle at $old_bundle. Extract the newly repaired publisher ZIP into a fresh folder, then rerun this script."
  die "cannot find $PUBLISHER_NAME. Put this script inside that folder, or set PUNPUN_PUBLISHER_DIR."
}

git_identity() {
  local repository=$1
  git -C "$repository" config user.name "PunPun Project"
  git -C "$repository" config user.email "punpun-project""@""users.noreply.github.com"
}

replace_checkout_contents() {
  local checkout=$1 source=$2
  [[ -n "$checkout" && -d "$checkout/.git" ]] || die "refusing to wipe '$checkout': not a checkout"
  find "$checkout" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf -- {} +
  cp -a "$source"/. "$checkout"/
}

# Returns the commit sha that ends up on the remote, so the caller can tag it.
SYNCED_SHA=''
sync_repository() {
  local repo_name=$1 source=$2 message=$3 pages=${4:-no}
  local checkout="$WORK/repos/$repo_name"
  SYNCED_SHA=''
  mkdir -p "$(dirname "$checkout")"

  if gh repo view "$GH_ACCOUNT/$repo_name" >/dev/null 2>&1; then
    # FIX 6: was `--depth 1`. A shallow clone cannot be pushed to some servers,
    # and the later `push origin HEAD:main` is a non-fast-forward the moment
    # main has moved. Clone the full history and push with a lease instead.
    gh repo clone "$GH_ACCOUNT/$repo_name" "$checkout" >/dev/null
    replace_checkout_contents "$checkout" "$source"
  else
    mkdir -p "$checkout"
    cp -a "$source"/. "$checkout"/
    git -C "$checkout" init -b main >/dev/null
  fi

  [[ "$pages" == yes ]] && : > "$checkout/.nojekyll"

  git_identity "$checkout"
  git -C "$checkout" add -A

  if git -C "$checkout" diff --cached --quiet && git -C "$checkout" rev-parse -q --verify HEAD >/dev/null; then
    SYNCED_SHA=$(git -C "$checkout" rev-parse HEAD)
    ok "$repo_name is already current"
    return
  fi

  git -C "$checkout" commit -m "$message" >/dev/null
  git -C "$checkout" branch -M main
  SYNCED_SHA=$(git -C "$checkout" rev-parse HEAD)

  if git -C "$checkout" remote get-url origin >/dev/null 2>&1; then
    git -C "$checkout" push --force-with-lease origin HEAD:main >/dev/null
  else
    gh repo create "$GH_ACCOUNT/$repo_name" --public --source="$checkout" --remote=origin --push >/dev/null
  fi
  ok "updated $GH_ACCOUNT/$repo_name"
}

# FIX 1: the release tag was never created explicitly. `gh release create`
# would invent it server-side at whatever the default branch happened to point
# at -- which, if the source push was skipped as "already current" or if CI had
# pushed in between, is not the tree these assets were built from.
ensure_release_tag() {
  local repo=$1 tag=$2 sha=$3
  [[ -n "$sha" ]] || { warn "no commit sha to tag; letting the release create $tag"; return; }

  local existing
  existing=$(gh api "repos/$repo/git/ref/tags/$tag" --jq .object.sha 2>/dev/null || true)
  if [[ "$existing" == "$sha" ]]; then
    ok "$tag already points at the published commit"
  elif [[ -n "$existing" ]]; then
    gh api --method PATCH "repos/$repo/git/refs/tags/$tag" \
      -f sha="$sha" -F force=true >/dev/null
    ok "moved $tag onto the published commit"
  else
    gh api --method POST "repos/$repo/git/refs" \
      -f "ref=refs/tags/$tag" -f sha="$sha" >/dev/null
    ok "created $tag on the published commit"
  fi
}

enable_pages() {
  local repo_name=$1
  # FIX 10: without build_type, repos previously configured for the Actions
  # Pages pipeline reject a branch source with 422.
  if gh api "repos/$GH_ACCOUNT/$repo_name/pages" >/dev/null 2>&1; then
    gh api --method PUT "repos/$GH_ACCOUNT/$repo_name/pages" \
      -f 'build_type=legacy' -f 'source[branch]=main' -f 'source[path]=/' >/dev/null
  else
    gh api --method POST "repos/$GH_ACCOUNT/$repo_name/pages" \
      -f 'build_type=legacy' -f 'source[branch]=main' -f 'source[path]=/' >/dev/null
  fi
  gh api --method POST "repos/$GH_ACCOUNT/$repo_name/pages/builds" >/dev/null 2>&1 || true
  ok "GitHub Pages enabled for $repo_name"
}

need bash; need git; need gh; need unzip
pick_sha_tool

ROOT=$(find_publisher)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/punpun-publish.XXXXXX")
# FIX 9: trapping EXIT plus the signals ran the cleanup twice and swallowed the
# signal exit status. Trap EXIT only; the shell reaches it on every path.
trap 'rm -rf -- "$WORK"' EXIT

gh auth status >/dev/null 2>&1 || die "GitHub CLI is not logged in; run 'gh auth login' first"
GH_ACCOUNT=$(gh api user --jq .login)

printf '%bPunPun %s publisher%b\n' "$green" "$VERSION" "$reset"
printf 'Account: %s\nBundle: %s\n' "$GH_ACCOUNT" "$ROOT"
print_cleanup_policy

step "Verifying every release file"
(cd "$ROOT" && "${SHA_CHECK[@]}" SHA256SUMS)
ok "release checksums passed"

step "Publishing the complete source repository"
mkdir -p "$WORK/source"
unzip -q "$ROOT/source/PunPun-${VERSION}-source.zip" -d "$WORK/source"
SOURCE_TREE="$WORK/source/PunPun-${VERSION}-source"
[[ -d "$SOURCE_TREE" ]] || die "source archive has an unexpected layout"
sanitize_publish_tree "$SOURCE_TREE" source
sync_repository "$SOURCE_REPO" "$SOURCE_TREE" "Publish PunPun $VERSION"
SOURCE_SHA=$SYNCED_SHA

# FIX 12: the old code fired `gh workflow run` and reported success from the
# exit status alone, so a source tree that had lost .github/workflows reported
# "started CI" while nothing ran. Check the workflow is actually there first.
if [[ -f "$SOURCE_TREE/.github/workflows/platform-release.yml" ]]; then
  if gh workflow run platform-release.yml --repo "$GH_ACCOUNT/$SOURCE_REPO" --ref main >/dev/null 2>&1; then
    ok "started fresh Linux, Arch and Windows CI validation"
  else
    warn "source was published, but CI could not be started; open the repository Actions tab"
  fi
else
  warn "no .github/workflows/platform-release.yml in the published tree; no CI was started"
fi

step "Publishing release downloads"
# FIX 5: nullglob was switched on globally and never restored, quietly changing
# glob behaviour for everything after this point. Scope it to the expansion.
collect_assets() {
  local -n out=$1
  local prev
  prev=$(shopt -p nullglob)
  shopt -s nullglob
  out=( "$ROOT"/linux/* "$ROOT"/arch/* "$ROOT"/editor/* "$ROOT"/windows/*
        "$ROOT"/websites/*.zip "$ROOT"/reports/* )
  eval "$prev"
}
collect_assets raw_assets

# FIX 4: `$ROOT/linux/*` also matches directories, and `gh release upload`
# fails on one. Keep regular files only.
assets=()
for candidate in "${raw_assets[@]}"; do
  [[ -f "$candidate" ]] && assets+=("$candidate")
done
assets+=("$ROOT/SHA256SUMS")
[[ -f "$ROOT/publish-punpun.sh" ]] && assets+=("$ROOT/publish-punpun.sh") \
                                   || assets+=("$SCRIPT_PATH")

# FIX 3: `(( ${#assets[@]} > 1 ))` always passed, because SHA256SUMS and this
# script are unconditionally appended. Require the platform payloads to exist.
missing=()
for category in linux arch editor windows websites; do
  compgen -G "$ROOT/$category/*" >/dev/null || missing+=("$category")
done
(( ${#missing[@]} == 0 )) || die "publisher bundle has no assets under: ${missing[*]}"

if gh release view "$TAG" --repo "$GH_ACCOUNT/$SOURCE_REPO" >/dev/null 2>&1; then
  ensure_release_tag "$GH_ACCOUNT/$SOURCE_REPO" "$TAG" "$SOURCE_SHA"
  prune_release_assets "$GH_ACCOUNT/$SOURCE_REPO" "$TAG" "${assets[@]}"
  gh release edit "$TAG" --repo "$GH_ACCOUNT/$SOURCE_REPO" \
    --title "PunPun $VERSION" --notes-file "$ROOT/RELEASE_NOTES.md" --prerelease >/dev/null
  gh release upload "$TAG" "${assets[@]}" --repo "$GH_ACCOUNT/$SOURCE_REPO" --clobber
  ok "updated release $TAG"
else
  ensure_release_tag "$GH_ACCOUNT/$SOURCE_REPO" "$TAG" "$SOURCE_SHA"
  gh release create "$TAG" "${assets[@]}" --repo "$GH_ACCOUNT/$SOURCE_REPO" \
    --title "PunPun $VERSION" --notes-file "$ROOT/RELEASE_NOTES.md" --prerelease
  ok "created release $TAG"
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

printf '\n%bEverything is published.%b\n' "$green" "$reset"
printf 'Source: https://github.com/%s/%s\n' "$GH_ACCOUNT" "$SOURCE_REPO"
printf 'Release: https://github.com/%s/%s/releases/tag/%s\n' "$GH_ACCOUNT" "$SOURCE_REPO" "$TAG"
printf 'Documentation: https://%s.github.io/%s/\n' "$GH_ACCOUNT" "$DOCS_REPO"
printf 'PPX catalog: https://%s.github.io/%s/\n' "$GH_ACCOUNT" "$PPX_REPO"
printf '\nGitHub Pages can take a minute or two to replace an earlier 404 page.\n'
