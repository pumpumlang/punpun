# Bugs fixed in `publish-punpun.sh`

Every item below is a real defect in the version currently at the repo root,
found by reading it rather than by running it (I had no way to execute a real
publish). `publisher/publish-punpun.sh` is a drop-in replacement with all of
them fixed; the fix numbers appear as `FIX n` comments in that file.

Ordered roughly by how much damage each one can do.

### 1. The release tag was never created — highest impact

`gh release create "$TAG" …` was called without the tag existing in git.
GitHub then creates the tag itself, at whatever the default branch points to
*at that instant*. That is not necessarily the tree the assets were built from:

* if `sync_repository` printed "already current" and skipped the push, the tag
  lands on whatever main already was;
* the script fires `gh workflow run platform-release.yml` immediately before
  this, so a CI job that pushes anything creates a race;
* on the update path (`gh release edit`) an existing tag is never re-pointed at
  all, so re-publishing the same version leaves the tag on the *old* commit
  while the assets are new.

Fixed by `ensure_release_tag`, which creates or force-moves the tag onto the
exact sha `sync_repository` pushed, before any release call.

### 2. `declare -A desired` in `prune_release_assets` is global

`declare` inside a function is global unless it is `local`. The array was
re-initialised each call, so this was latent rather than active — but
`keep=${desired["$current"]:-}` also errors under `set -u` on bash 4.3 for a
key that was never set. Now `local -A` with `[[ -v … ]]`.

### 3. The "release assets are missing" guard could not fire

```bash
(( ${#assets[@]} > 1 )) || die "release assets are missing"
```

`$ROOT/SHA256SUMS` and `$PUBLISH_SCRIPT` are appended unconditionally, so the
array always has at least 2 entries. With `nullglob` on, every platform glob
could expand to nothing and the check would still pass — publishing a release
containing only a checksum file and the publisher script. Now each category is
verified with `compgen -G` individually.

### 4. Directories could be passed to `gh release upload`

`"$ROOT"/linux/*` matches subdirectories too, and `gh release upload` errors
on a directory argument. Now filtered to regular files.

### 5. `shopt -s nullglob` set globally, never restored

Turned on mid-script and left on, silently changing glob semantics for
everything after it. Now scoped to the one expansion that needs it, with the
previous setting restored.

### 6. Shallow clone, then push

`gh repo clone … -- --depth 1` followed by `git push origin HEAD:main`. Shallow
pushes are rejected outright by some servers, and the push is a plain
non-fast-forward as soon as main has moved (which the CI trigger makes likely).
Now a full clone plus `--force-with-lease`.

### 7. `need sha256sum` fails on macOS

macOS ships `shasum`, not `sha256sum`, so the script refused to run on a Mac
that was perfectly capable of verifying the bundle. Now it accepts either.

### 8. Cleanup deleted binary test fixtures

`sanitize_publish_tree … source` ran `find -type f \( -name '*.o' -o -name
'*.a' -o -name '*.so' -o -name '*.dll' -o -name '*.exe' \) -delete` across the
whole tree. A compiler test suite that checks linking against a fixture object
or a prebuilt archive loses it, and the failure surfaces later as a mysterious
CI break on a tree that was fine locally. Now filtered through an explicit
protected-prefix list (`tests/ spec/ examples/ stdlib/ .github/`).

### 9. `trap … EXIT HUP INT TERM` runs twice

On `INT` the handler runs, the shell then exits, and the `EXIT` trap runs it
again. Harmless for `rm -rf`, but it also discards the signal exit status, so a
Ctrl-C looked like a clean exit to anything wrapping the script. Now `EXIT`
only, which is reached on every path anyway.

### 10. `enable_pages` omitted `build_type`

A repo previously configured for the Actions-based Pages pipeline rejects a
branch source with HTTP 422 unless `build_type=legacy` is sent. Added to both
the POST and PUT branches.

### 11. `RELEASE_NOTES.md` was never validated

`find_publisher` accepted a bundle on the strength of `SHA256SUMS`, `source/`
and `websites/`, but `gh release edit --notes-file "$ROOT/RELEASE_NOTES.md"`
runs much later. A bundle missing it failed *after* the source repo had already
been overwritten and CI started. Now part of the bundle test.

### 12. "started CI" was reported when no workflow existed

`replace_checkout_contents` wipes the checkout down to `.git`, so if the source
archive lacks `.github/workflows`, the remote loses it. The next line's
`gh workflow run … >/dev/null 2>&1` then fails, and the `else` branch says
"CI could not be started automatically" — but the far more likely cause, that
the workflow file is simply gone, is never surfaced. Now the file's presence is
checked and reported distinctly.

### 13. `VERSION` was only stripped of CR/LF

`tr -d '\r\n'` leaves spaces and tabs. A trailing space produces `TAG="v0.6.0-beta "`,
which silently matches no release and creates a junk tag. Now `tr -d '[:space:]'`.

### 14. `replace_checkout_contents` had no guard

It runs `find "$checkout" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf`.
`set -u` protects against the variable being *unset*, but not against it being
a wrong-but-set path. Now it refuses to run unless the target contains `.git`.
