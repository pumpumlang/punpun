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


def markdown_files() -> list[Path]:
    roots = [ROOT / "README.md", ROOT / "docs", ROOT / "docs-site" / "content", ROOT / "packages"]
    result: list[Path] = []
    for root in roots:
        if root.is_file():
            result.append(root)
        elif root.is_dir():
            result.extend(sorted(root.rglob("*.md")))
    return sorted(set(result))


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
    args = parser.parse_args()
    total = 0
    for path in markdown_files():
        for mode, line, source in blocks(path):
            total += 1
            with tempfile.TemporaryDirectory(prefix="punpun-doctest-") as td:
                source_path = Path(td) / "main.pp"
                source_path.write_text(source, encoding="utf-8")
                command = [args.ppc, "run" if mode == "doctest-run" else "check", str(source_path)]
                result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, timeout=30)
                if result.returncode != 0:
                    raise SystemExit(
                        f"doctest failed: {path.relative_to(ROOT)}:{line}\n"
                        f"--- stdout ---\n{result.stdout}\n--- stderr ---\n{result.stderr}"
                    )
    if total == 0:
        raise SystemExit("no PunPun doctest blocks found")
    print(f"{total} PunPun doctest(s) passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
