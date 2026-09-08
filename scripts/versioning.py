"""Canonical PunPun release-version helpers.

The repository-root VERSION file is the only hand-edited version source.
Generated mirrors are checked by scripts/check_version.py.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SEMVER = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-([0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*))?$"
)


def read_version() -> str:
    value = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not SEMVER.fullmatch(value):
        raise RuntimeError(f"VERSION is not valid PunPun semver: {value!r}")
    return value


def arch_version(version: str | None = None) -> str:
    return (version or read_version()).replace("-", "_")


def numeric_version(version: str | None = None) -> str:
    return (version or read_version()).split("-", 1)[0]


VERSION = read_version()
PKGVER = arch_version(VERSION)
NUMERIC_VERSION = numeric_version(VERSION)
