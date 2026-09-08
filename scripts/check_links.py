#!/usr/bin/env python3
"""Validate repository-relative Markdown links and images without network access."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from urllib.parse import unquote

LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
SKIP_SCHEMES = ("http://", "https://", "mailto:", "tel:", "data:")


def markdown_files(path: Path):
    if path.is_file():
        if path.suffix.lower() == ".md":
            yield path
        return
    if path.is_dir():
        yield from sorted(path.rglob("*.md"))


def clean_target(raw: str) -> str:
    raw = raw.strip()
    if raw.startswith("<") and raw.endswith(">"):
        raw = raw[1:-1]
    # Markdown permits an optional title after whitespace. PunPun docs do not
    # rely on spaces in unescaped local filenames, so split only quoted titles.
    raw = re.sub(r'\s+["\'][^"\']*["\']\s*$', "", raw)
    return unquote(raw)


def resolve(repo: Path, source: Path, target: str) -> Path | None:
    if not target or target.startswith("#") or target.lower().startswith(SKIP_SCHEMES):
        return None
    if target.startswith("//"):
        return None
    path_part = target.split("#", 1)[0].split("?", 1)[0]
    if not path_part:
        return None
    if any(ch in path_part for ch in "{}*$|"):
        return None
    candidate = (repo / path_part.lstrip("/")) if path_part.startswith("/") else (source.parent / path_part)
    return candidate.resolve()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path.cwd())
    args = parser.parse_args()
    repo = args.repo_root.resolve()
    failures: list[str] = []
    checked = 0
    seen: set[Path] = set()

    for supplied in args.paths:
        path = supplied if supplied.is_absolute() else repo / supplied
        for md in markdown_files(path):
            md = md.resolve()
            if md in seen:
                continue
            seen.add(md)
            text = md.read_text(encoding="utf-8")
            for line_no, line in enumerate(text.splitlines(), 1):
                for match in LINK.finditer(line):
                    target = clean_target(match.group(1))
                    candidate = resolve(repo, md, target)
                    if candidate is None:
                        continue
                    checked += 1
                    try:
                        candidate.relative_to(repo)
                    except ValueError:
                        failures.append(f"{md.relative_to(repo)}:{line_no}: link escapes repository: {target}")
                        continue
                    if not candidate.exists():
                        # docs-site source intentionally links to generated .html
                        # pages while the repository stores the authoring source
                        # as sibling .md files. Treat that mapping as valid, but
                        # only when the corresponding Markdown source exists.
                        generated_source = candidate.with_suffix(".md") if candidate.suffix.lower() == ".html" else None
                        if generated_source is None or not generated_source.exists():
                            failures.append(f"{md.relative_to(repo)}:{line_no}: missing relative target: {target}")

    if failures:
        print("relative link check failed", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print(f"relative link check passed: {checked} local links across {len(seen)} Markdown files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
