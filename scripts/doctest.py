#!/usr/bin/env python3
"""Compile/run explicitly marked PunPun Markdown examples."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OPEN_RE = re.compile(r"^```(?:punpun|pp)\s+(doctest|doctest-run)\s*$", re.I)


SKIP_DIRS = {".git", "build", "dist", "node_modules", "__pycache__", ".punpun"}


def markdown_files(base: Path) -> list[Path]:
    if base.is_file():
        return [base]
    result: list[Path] = []
    for path in sorted(base.rglob("*.md")):
        if SKIP_DIRS.isdisjoint(path.relative_to(base).parts):
            result.append(path)
    return result


def blocks(path: Path):
    lines = path.read_text(encoding="utf-8").splitlines()
    i = 0
    while i < len(lines):
        match = OPEN_RE.match(lines[i].strip())
        if not match:
            i += 1
            continue
        mode = match.group(1).lower()
        start = i + 2
        i += 1
        body = []
        while i < len(lines) and lines[i].strip() != "```":
            body.append(lines[i])
            i += 1
        if i >= len(lines):
            raise SystemExit(f"unterminated doctest fence in {path}:{start}")
        yield mode, start, "\n".join(body) + "\n"
        i += 1


def main() -> int:
    parser = argparse.ArgumentParser(description="run PunPun documentation examples")
    parser.add_argument("--ppc", default=str(ROOT / "build" / "ppc"))
    parser.add_argument("--root", default=str(ROOT), metavar="PATH",
                        help="directory (or single Markdown file) to scan; defaults to the toolchain")
    parser.add_argument("--require-blocks", action="store_true",
                        help="fail when no doctest blocks are found")
    args = parser.parse_args()
    base = Path(args.root).resolve()
    if not base.exists():
        raise SystemExit(f"doctest: no such path: {base}")
    total = 0
    for path in markdown_files(base):
        for mode, line, source in blocks(path):
            total += 1
            with tempfile.TemporaryDirectory(prefix="punpun-doctest-") as td:
                source_path = Path(td) / "main.pp"
                source_path.write_text(source, encoding="utf-8")
                command = [args.ppc, "run" if mode == "doctest-run" else "check", str(source_path)]
                result = subprocess.run(command, cwd=td, text=True, capture_output=True, timeout=30)
                if result.returncode != 0:
                    raise SystemExit(
                        f"doctest failed: {path}:{line}\n"
                        f"--- stdout ---\n{result.stdout}\n--- stderr ---\n{result.stderr}"
                    )
    if total == 0:
        if args.require_blocks:
            raise SystemExit(f"no PunPun doctest blocks found under {base}")
        print(f"no PunPun doctest blocks found under {base}")
        return 0
    print(f"{total} PunPun doctest(s) passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
