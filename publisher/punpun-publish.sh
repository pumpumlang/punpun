#!/usr/bin/env bash
#
# punpun-publish -- take a working checkout from "edited" to "published".
#
#   1. preflight   verify tools, branch, and that we are in the right repo
#   2. generate    rebuild every brand asset from scripts/build_brand.py
#   3. patch       wire the artwork into the VS Code manifest
#   4. prune       delete build junk (and, opt-in, stale reports)
#   5. verify      run the repo's own checks
#   6. commit      stage everything and write one commit
#   7. tag         annotated vVERSION tag on that exact commit
#   8. push        branch first, then tag
#
# Nothing leaves the machine unless --push is given, and nothing is deleted or
# committed at all under --dry-run.
#
#   ./publisher/punpun-publish.sh --dry-run
#   ./publisher/punpun-publish.sh -m "Refresh brand assets"
#   ./publisher/punpun-publish.sh --include-stale --tag --push
#
set -Eeuo pipefail

SELF=${BASH_SOURCE[0]}
PUBLISHER_DIR=$(CDPATH= cd -- "$(dirname -- "$SELF")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$PUBLISHER_DIR/.." && pwd)

DRY_RUN=0
DO_TAG=0
DO_PUSH=0
DO_VERIFY=1
DO_GENERATE=1
INCLUDE_STALE=0
ASSUME_YES=0
ALLOW_DIRTY_BRANCH=0
REMOTE=origin
BRANCH=
MESSAGE=

if [[ -t 1 ]]; then
  c_g=$'\033[1;32m'; c_b=$'\033[1;36m'; c_y=$'\033[1;33m'; c_r=$'\033[1;31m'; c_0=$'\033[0m'
else
  c_g=; c_b=; c_y=; c_r=; c_0=
fi
step() { printf '\n%s==>%s %s\n' "$c_b" "$c_0" "$*"; }
ok()   { printf '%s  ok%s %s\n' "$c_g" "$c_0" "$*"; }
warn() { printf '%s   !%s %s\n' "$c_y" "$c_0" "$*" >&2; }
die()  { printf '%spunpun-publish:%s %s\n' "$c_r" "$c_0" "$*" >&2; exit 1; }
run()  { if (( DRY_RUN )); then printf '       would run: %s\n' "$*"; else "$@"; fi; }

usage() {
  sed -n '2,/^set -Eeuo/p' "$SELF" | sed 's/^# \{0,1\}//; $d'
  cat <<'EOF'
Options:
  -m, --message TEXT   commit message (default: generated)
  -b, --branch NAME    branch to commit on (default: current)
  -r, --remote NAME    remote to push to (default: origin)
      --dry-run        print every action, change nothing
      --include-stale  also delete the [stale] entries in clean-manifest.txt
      --tag            create/move the annotated vVERSION tag
      --push           push the branch (and the tag, with --tag)
      --no-verify      skip the repo's own test/check step
      --no-generate    do not regenerate brand assets
      --allow-branch   proceed even on a non-default branch
  -y, --yes            do not prompt
  -h, --help           this text
EOF
}

while (( $# )); do
  case $1 in
    -m|--message)     MESSAGE=${2:?--message needs a value}; shift 2 ;;
    -b|--branch)      BRANCH=${2:?--branch needs a value}; shift 2 ;;
    -r|--remote)      REMOTE=${2:?--remote needs a value}; shift 2 ;;
    --dry-run)        DRY_RUN=1; shift ;;
    --include-stale)  INCLUDE_STALE=1; shift ;;
    --tag)            DO_TAG=1; shift ;;
    --push)           DO_PUSH=1; shift ;;
    --no-verify)      DO_VERIFY=0; shift ;;
    --no-generate)    DO_GENERATE=0; shift ;;
    --allow-branch)   ALLOW_DIRTY_BRANCH=1; shift ;;
    -y|--yes)         ASSUME_YES=1; shift ;;
    -h|--help)        usage; exit 0 ;;
    *)                die "unknown option '$1' (try --help)" ;;
  esac
done

need() { command -v "$1" >/dev/null 2>&1 || die "missing required tool '$1'"; }

# ---------------------------------------------------------------- 1. preflight
step "Preflight"
need git
need python3

git -C "$REPO_ROOT" rev-parse --git-dir >/dev/null 2>&1 \
  || die "$REPO_ROOT is not a git checkout"

# Refuse to operate on some *other* repo that happens to contain this script.
[[ -f "$REPO_ROOT/VERSION" ]] \
  || die "no VERSION file at $REPO_ROOT — this does not look like the punpun repo"

VERSION=$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")
[[ -n "$VERSION" ]] || die "VERSION is empty"
TAG="v${VERSION}"

CURRENT_BRANCH=$(git -C "$REPO_ROOT" symbolic-ref --quiet --short HEAD || echo '')
[[ -n "$CURRENT_BRANCH" ]] || die "HEAD is detached; check out a branch first"
BRANCH=${BRANCH:-$CURRENT_BRANCH}

if [[ "$BRANCH" != "$CURRENT_BRANCH" ]]; then
  die "asked to commit on '$BRANCH' but '$CURRENT_BRANCH' is checked out"
fi
if (( ! ALLOW_DIRTY_BRANCH )) && [[ "$BRANCH" != main && "$BRANCH" != master ]]; then
  die "on branch '$BRANCH'; pass --allow-branch if that is intended"
fi

ok "repo   $REPO_ROOT"
ok "branch $BRANCH"
ok "version $VERSION (tag $TAG)"
(( DRY_RUN )) && warn "dry run: nothing will be deleted, committed or pushed"

# ---------------------------------------------------------------- 2. generate
if (( DO_GENERATE )); then
  step "Regenerating brand assets"
  if [[ -f "$REPO_ROOT/scripts/build_brand.py" ]]; then
    brand_extra=()
    # shellcheck disable=SC2206
    [[ -n "${PUNPUN_BRAND_ARGS:-}" ]] && brand_extra=(${PUNPUN_BRAND_ARGS})
    if ! python3 -c 'import PIL' 2>/dev/null && [[ ${#brand_extra[@]} -eq 0 ]]; then
      brand_extra=(--svg-only)
      warn "Pillow not installed; generating SVG assets only (no PNG icons)"
    fi
    run python3 "$REPO_ROOT/scripts/build_brand.py" --repo-root "$REPO_ROOT" "${brand_extra[@]}"
    ok "assets rebuilt from source geometry"
  else
    warn "scripts/build_brand.py not found; skipping asset generation"
  fi
fi

# ---------------------------------------------------------------- 3. patch
step "Patching the VS Code extension manifest"
if [[ -f "$REPO_ROOT/editors/vscode/package.json" ]]; then
  if [[ -f "$REPO_ROOT/scripts/patch_extension_manifest.py" ]]; then
    if (( DRY_RUN )); then
      python3 "$REPO_ROOT/scripts/patch_extension_manifest.py" \
        --repo-root "$REPO_ROOT" --check || true
    else
      python3 "$REPO_ROOT/scripts/patch_extension_manifest.py" --repo-root "$REPO_ROOT"
    fi
  else
    warn "scripts/patch_extension_manifest.py not found; skipping"
  fi
else
  warn "no editors/vscode/package.json; skipping extension manifest"
fi

# ---------------------------------------------------------------- 4. prune
step "Removing build junk"
prune_args=(--repo-root "$REPO_ROOT" --manifest "$PUBLISHER_DIR/clean-manifest.txt")
(( DRY_RUN ))       && prune_args+=(--dry-run)
(( INCLUDE_STALE )) && prune_args+=(--include-stale)
python3 "$PUBLISHER_DIR/prune.py" "${prune_args[@]}"

# ---------------------------------------------------------------- 5. verify
if (( DO_VERIFY )); then
  step "Running repository checks"
  ran_any=0
  failed=''
  if [[ -f "$REPO_ROOT/scripts/check_version.py" ]]; then
    if (( DRY_RUN )); then
      printf '       would run: python3 scripts/check_version.py\n'
    elif ! python3 "$REPO_ROOT/scripts/check_version.py"; then
      failed="scripts/check_version.py"
    fi
    ran_any=1
  fi
  if [[ -z "$failed" ]] && [[ -f "$REPO_ROOT/Makefile" ]] && grep -qE '^test:' "$REPO_ROOT/Makefile"; then
    if (( DRY_RUN )); then
      printf '       would run: make test\n'
    elif ! make -C "$REPO_ROOT" test; then
      failed="make test"
    fi
    ran_any=1
  fi

  if [[ -n "$failed" ]]; then
    # A failing check is a real signal, so this stops rather than warns. But it
    # is often just an unbuilt tree rather than broken code, so say so.
    printf '\n'
    warn "'$failed' failed."
    warn ""
    warn "If this is a fresh clone, the checks likely need a built compiler:"
    warn "    make -C $REPO_ROOT compiler"
    warn "then rerun. To publish without running them:"
    warn "    $0 --no-verify ...  (or PUNPUN_SKIP_CHECKS=1 with PUBLISH.sh)"
    die "stopping before the commit. Cleanup already applied to the working tree is left in place; 'git checkout -- .' reverts it."
  fi

  (( ran_any )) && ok "checks passed" || warn "no checks found to run"
else
  warn "verification skipped (--no-verify)"
fi

# ---------------------------------------------------------------- 6. commit
step "Committing"
git -C "$REPO_ROOT" add -A

if git -C "$REPO_ROOT" diff --cached --quiet; then
  ok "working tree already matches HEAD; nothing to commit"
  COMMITTED=0
else
  changed=$(git -C "$REPO_ROOT" diff --cached --numstat | wc -l | tr -d ' ')
  git -C "$REPO_ROOT" diff --cached --stat | tail -n 20

  if [[ -z "$MESSAGE" ]]; then
    MESSAGE=$(printf 'Publish PunPun %s\n\nRegenerated brand assets, refreshed the VS Code extension icon and\nfile-icon theme, and removed generated build output from the tree.\n' "$VERSION")
  fi

  if (( ! ASSUME_YES && ! DRY_RUN )); then
    printf '\nCommit %s file(s) on %s? [y/N] ' "$changed" "$BRANCH"
    read -r reply </dev/tty || reply=n
    [[ $reply == [yY]* ]] || die "aborted before committing"
  fi

  if (( DRY_RUN )); then
    printf '       would commit %s file(s)\n' "$changed"
    COMMITTED=1
  else
    git -C "$REPO_ROOT" -c user.useConfigOnly=false commit -q -m "$MESSAGE"
    ok "committed $(git -C "$REPO_ROOT" rev-parse --short HEAD)"
    COMMITTED=1
  fi
fi

# ---------------------------------------------------------------- 7. tag
if (( DO_TAG )); then
  step "Tagging $TAG"
  if git -C "$REPO_ROOT" rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    existing=$(git -C "$REPO_ROOT" rev-list -n1 "$TAG")
    head=$(git -C "$REPO_ROOT" rev-parse HEAD)
    if [[ "$existing" == "$head" ]]; then
      ok "$TAG already points at HEAD"
    else
      warn "$TAG exists and points elsewhere; moving it"
      run git -C "$REPO_ROOT" tag -f -a "$TAG" -m "PunPun $VERSION"
    fi
  else
    run git -C "$REPO_ROOT" tag -a "$TAG" -m "PunPun $VERSION"
    ok "created $TAG on this commit"
  fi
fi

# ---------------------------------------------------------------- 8. push
if (( DO_PUSH )); then
  step "Pushing"
  git -C "$REPO_ROOT" remote get-url "$REMOTE" >/dev/null 2>&1 \
    || die "no remote named '$REMOTE'"

  # Fetch first so --force-with-lease has a real baseline to compare against.
  run git -C "$REPO_ROOT" fetch --quiet "$REMOTE" "$BRANCH" || true
  run git -C "$REPO_ROOT" push --force-with-lease "$REMOTE" "$BRANCH"
  ok "pushed $BRANCH to $REMOTE"

  # The tag goes last and separately: pushing branch and tag together means a
  # rejected tag can leave the branch unpushed, or vice versa.
  if (( DO_TAG )); then
    run git -C "$REPO_ROOT" push --force "$REMOTE" "refs/tags/$TAG"
    ok "pushed $TAG"
  fi
else
  if (( COMMITTED )); then
    printf '\n%sLocal only.%s Review with: git -C %q log -1 -p | head -n 60\n' \
      "$c_y" "$c_0" "$REPO_ROOT"
    printf 'Then publish with: %s --push%s\n' "$SELF" "$( ((DO_TAG)) && echo ' --tag')"
  fi
fi

printf '\n%sDone.%s\n' "$c_g" "$c_0"
