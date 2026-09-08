#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import re


IGNORED_PARTS = {".git", ".punpun", "build", "dist", "__pycache__", ".pytest_cache", "node_modules"}
TEXT_LIMIT = 4 * 1024 * 1024
PRIVATE_PATTERNS = {
    "email address": re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}"),
    "Linux home path": re.compile(r"/home/[A-Za-z0-9._-]+"),
    "Windows user path": re.compile(r"(?i)[A-Z]:[/\\]Users[/\\][A-Za-z0-9._ -]+"),
    "workspace path": re.compile(r"/workspace/(?:scratch|home)/"),
    "GitHub token": re.compile(r"(?:ghp_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})"),
    "private key": re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----"),
}
FORBIDDEN_SUFFIXES = {".bak", ".orig", ".rej", ".pyc", ".tmp"}
FORBIDDEN_NAMES = {".env", "credentials", "id_rsa", "id_ed25519", ".DS_Store", "Thumbs.db", "desktop.ini", ".coverage"}


def forbidden_filename(path: Path) -> bool:
    name = path.name
    lower = name.lower()
    if name in FORBIDDEN_NAMES or path.suffix.lower() in FORBIDDEN_SUFFIXES:
        return True
    return (
        ".bak-" in lower
        or lower.endswith((".swp", ".swo", "~"))
    )


def files_under(root: Path):
    for path in sorted(root.rglob("*")):
        if not path.is_file() or any(part in IGNORED_PARTS for part in path.parts):
            continue
        yield path


def main() -> int:
    parser = argparse.ArgumentParser(description="Fail when release source contains private identity or build debris.")
    parser.add_argument("root", nargs="?", type=Path, default=Path.cwd())
    args = parser.parse_args()
    root = args.root.resolve()
    findings: list[str] = []
    scanned = 0
    for path in files_under(root):
        relative = path.relative_to(root)
        if forbidden_filename(path):
            findings.append(f"generated/private filename: {relative}")
            continue
        if path.stat().st_size > TEXT_LIMIT:
            continue
        data = path.read_bytes()
        if b"\0" in data:
            continue
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        scanned += 1
        for label, pattern in PRIVATE_PATTERNS.items():
            match = pattern.search(text)
            if match:
                findings.append(f"{label}: {relative}:{text.count(chr(10), 0, match.start()) + 1}")
    if findings:
        print("privacy audit failed")
        for finding in findings:
            print(f"- {finding}")
        return 1
    print(f"privacy audit passed: {scanned} text files; no identity paths, emails, tokens, private keys, or debris")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
