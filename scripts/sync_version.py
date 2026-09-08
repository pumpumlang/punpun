#!/usr/bin/env python3
"""Synchronize generated version mirrors from the root VERSION file."""
from __future__ import annotations

import json
from pathlib import Path

from versioning import ROOT, VERSION


def write_if_changed(path: Path, text: str) -> None:
    if not text.endswith("\n"):
        text += "\n"
    if path.is_file() and path.read_text(encoding="utf-8") == text:
        return
    path.write_text(text, encoding="utf-8")


def main() -> None:
    for relative in ("runtime/VERSION", "stdlib/VERSION"):
        write_if_changed(ROOT / relative, VERSION)

    package_path = ROOT / "editors/vscode/package.json"
    package = json.loads(package_path.read_text(encoding="utf-8"))
    package["version"] = VERSION
    write_if_changed(package_path, json.dumps(package, indent=2, ensure_ascii=False))
    print(f"synchronized generated version mirrors -> {VERSION}")


if __name__ == "__main__":
    main()
