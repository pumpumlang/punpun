#!/usr/bin/env python3
"""Fail when a generated version surface drifts from root VERSION."""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

from versioning import ROOT, VERSION


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> None:
    for relative in ("runtime/VERSION", "stdlib/VERSION"):
        value = (ROOT / relative).read_text(encoding="utf-8").strip()
        require(value == VERSION, f"{relative} has {value!r}; expected {VERSION!r}")

    package = json.loads((ROOT / "editors/vscode/package.json").read_text(encoding="utf-8"))
    require(package.get("version") == VERSION, "VS Code extension version is out of sync")

    compiler = ROOT / "build/ppc"
    if sys.platform == "win32":
        compiler = compiler.with_suffix(".exe")
    require(compiler.is_file(), "build the compiler before running the version check")
    reported = subprocess.run(
        [str(compiler), "--version"], cwd=ROOT, check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout.strip()
    require(reported == f"ppc {VERSION}", f"compiler reports {reported!r}")

    info = json.loads(subprocess.run(
        [str(compiler), "language-info"], cwd=ROOT, check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout)
    require(info.get("compiler_version") == VERSION, "language-info version is out of sync")
    if VERSION.startswith("1."):
        require(info.get("language_version") == "1.0", "language-info stable language version is wrong")
        require(info.get("language_stability") == "stable", "language-info must report stable for 1.x")
        require(info.get("abi_version") == 1, "PunPun 1.x ABI version must remain 1")
        require(info.get("runtime_abi_version") == 1, "PunPun 1.x runtime ABI version must remain 1")
        require(info.get("lockfile_format") == 1, "PunPun 1.x lockfile format must remain 1")
        require(info.get("package_format") == 1, "PunPun 1.x package format must remain 1")

    ppx = subprocess.run(
        [sys.executable, str(ROOT / "ppx/ppx.py"), "--version"], cwd=ROOT,
        check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout.strip()
    require(VERSION in ppx, f"PPX reports the wrong version: {ppx!r}")
    print(f"all version surfaces agree on {VERSION}")


if __name__ == "__main__":
    main()
