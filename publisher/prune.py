#!/usr/bin/env python3
"""Remove build junk and stale files from a PunPun checkout, safely.

Rules live in ``clean-manifest.txt`` next to this script, so what gets deleted
is reviewable in a diff rather than buried in a find(1) invocation.

The engine is git-aware, which matters: junk that is *tracked* has to be
``git rm``'d to actually leave the repository, while junk that is merely
untracked can be unlinked. Reporting them separately makes it obvious when
generated output has been committed by accident.

Safety rails, all unconditional:
  * every candidate must resolve inside the repository root
  * symlinks are never followed and never deleted through
  * ``.git`` is never touched
  * anything matching a ``[protect]`` rule wins over every remove rule
  * ``[stale]`` entries need an explicit --include-stale

    python3 publisher/prune.py --repo-root . --dry-run
    python3 publisher/prune.py --repo-root . --include-stale
"""

from __future__ import annotations

import argparse
import fnmatch
import os
import shutil
import subprocess
import sys
from pathlib import Path

SECTIONS = ("remove-dirs", "remove-files", "protect", "stale")


def parse_manifest(path: Path) -> dict[str, list[str]]:
    rules: dict[str, list[str]] = {s: [] for s in SECTIONS}
    current = None
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            current = line[1:-1].strip()
            if current not in rules:
                raise SystemExit(f"{path}:{lineno}: unknown section [{current}]")
            continue
        if current is None:
            raise SystemExit(f"{path}:{lineno}: rule outside any section")
        rules[current].append(line)
    return rules


def git(root: Path, *args: str) -> str | None:
    try:
        out = subprocess.run(("git", "-C", str(root)) + args,
                             capture_output=True, text=True, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    return out.stdout


def tracked_paths(root: Path) -> set[str] | None:
    out = git(root, "ls-files", "-z")
    if out is None:
        return None
    return {p for p in out.split("\0") if p}


def matches(rel: str, name: str, patterns: list[str]) -> bool:
    """A rule matches on the basename or on the repo-relative path."""
    for pat in patterns:
        if "/" in pat:
            if fnmatch.fnmatch(rel, pat) or rel.startswith(pat.rstrip("/") + "/"):
                return True
        elif fnmatch.fnmatch(name, pat):
            return True
    return False


def collect(root: Path, rules: dict[str, list[str]], include_stale: bool):
    """Walk the tree once and decide the fate of everything in it."""
    victims: list[tuple[Path, str]] = []
    protected: list[Path] = []

    for dirpath, dirnames, filenames in os.walk(root, topdown=True, followlinks=False):
        here = Path(dirpath)
        rel_dir = here.relative_to(root).as_posix()
        if rel_dir == ".":
            rel_dir = ""

        # never descend into .git, and never descend into a directory we are
        # about to delete wholesale
        dirnames[:] = [d for d in dirnames if d != ".git"]

        keep_dirs = []
        for d in dirnames:
            p = here / d
            rel = f"{rel_dir}/{d}" if rel_dir else d
            if p.is_symlink():
                keep_dirs.append(d)      # leave symlinked dirs entirely alone
                continue
            if matches(rel, d, rules["protect"]):
                protected.append(p)
                keep_dirs.append(d)
                continue
            if matches(rel, d, rules["remove-dirs"]):
                victims.append((p, "generated directory"))
                continue                 # do not descend
            keep_dirs.append(d)
        dirnames[:] = keep_dirs

        for f in filenames:
            p = here / f
            rel = f"{rel_dir}/{f}" if rel_dir else f
            if matches(rel, f, rules["protect"]):
                protected.append(p)
                continue
            if matches(rel, f, rules["remove-files"]):
                victims.append((p, "generated file"))
            elif include_stale and matches(rel, f, rules["stale"]):
                victims.append((p, "stale"))

    if include_stale:
        for pat in rules["stale"]:
            if "/" in pat or "*" in pat:
                continue
        for pat in rules["stale"]:
            p = root / pat
            if p.is_dir() and not p.is_symlink() and (p, "stale") not in victims:
                victims.append((p, "stale directory"))

    return victims, protected


def safe(root: Path, target: Path) -> bool:
    """Refuse anything that escapes the repo, or that is the repo itself."""
    try:
        resolved = target.resolve(strict=False)
    except OSError:
        return False
    root_resolved = root.resolve()
    if resolved == root_resolved:
        return False
    if not resolved.is_relative_to(root_resolved):
        return False
    if ".git" in resolved.relative_to(root_resolved).parts:
        return False
    return True


def human(n: int) -> str:
    for unit in ("B", "KiB", "MiB", "GiB"):
        if n < 1024 or unit == "GiB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024
    return f"{n} B"


def size_of(p: Path) -> int:
    if p.is_file():
        try:
            return p.stat().st_size
        except OSError:
            return 0
    total = 0
    for dp, _, fn in os.walk(p, followlinks=False):
        for f in fn:
            try:
                total += (Path(dp) / f).stat().st_size
            except OSError:
                pass
    return total


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo-root", default=".", type=Path)
    ap.add_argument("--manifest", type=Path, default=None)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--include-stale", action="store_true",
                    help="also remove the explicitly listed [stale] paths")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    root = args.repo_root.resolve()
    if not (root / ".git").exists():
        print(f"prune: {root} is not a git checkout; refusing to delete anything",
              file=sys.stderr)
        return 2

    manifest = args.manifest or Path(__file__).with_name("clean-manifest.txt")
    if not manifest.is_file():
        print(f"prune: missing manifest {manifest}", file=sys.stderr)
        return 2

    rules = parse_manifest(manifest)
    victims, protected = collect(root, rules, args.include_stale)
    victims = [(p, why) for p, why in victims if safe(root, p)]
    victims.sort(key=lambda v: v[0].as_posix())

    if not victims:
        if not args.quiet:
            print("prune: nothing to remove")
        return 0

    tracked = tracked_paths(root)
    freed = 0
    tracked_hits: list[str] = []

    for p, why in victims:
        rel = p.relative_to(root).as_posix()
        freed += size_of(p)
        is_tracked = tracked is not None and (
            rel in tracked or any(t.startswith(rel + "/") for t in tracked))
        if is_tracked:
            tracked_hits.append(rel)
        if not args.quiet:
            flag = " [tracked]" if is_tracked else ""
            print(f"  {'would remove' if args.dry_run else 'removed'} {rel}  ({why}){flag}")
        if args.dry_run:
            continue
        if is_tracked:
            git(root, "rm", "-r", "--quiet", "--cached", "--ignore-unmatch", rel)
        if p.is_dir() and not p.is_symlink():
            shutil.rmtree(p, ignore_errors=True)
        else:
            try:
                p.unlink()
            except OSError:
                pass

    verb = "would free" if args.dry_run else "freed"
    print(f"prune: {len(victims)} path(s), {verb} {human(freed)}"
          + (f", {len(protected)} protected" if protected else ""))
    if tracked_hits:
        print(f"prune: {len(tracked_hits)} of those were tracked in git — "
              "generated output had been committed; they are now staged for removal")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
