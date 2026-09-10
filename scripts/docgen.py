#!/usr/bin/env python3
"""Generate deterministic PunPun API reference from first-party .pp sources."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DECL_RE = re.compile(
    r"^\s*(?P<prefix>(?:public\s+|private\s+)?(?:async\s+)?(?:extern\s+native\s+)?)"
    r"(?P<kind>fn|object|struct|enum|contract)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"(?P<tail>.*)$"
)


def source_files() -> list[Path]:
    files = sorted((ROOT / "stdlib" / "std").rglob("*.pp"))
    files += sorted((ROOT / "packages").glob("*/src/main.pp"))
    return files


def extract(path: Path) -> list[dict[str, object]]:
    entries: list[dict[str, object]] = []
    comments: list[str] = []
    for line_no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = raw.strip()
        if stripped.startswith("#"):
            comments.append(stripped[1:].strip())
            continue
        match = DECL_RE.match(raw)
        if not match:
            if stripped:
                comments = []
            continue
        prefix = match.group("prefix").strip()
        signature = (prefix + " " if prefix else "") + match.group("kind") + " " + match.group("name") + match.group("tail")
        signature = signature.split("{")[0].strip()
        entries.append({
            "name": match.group("name"),
            "kind": match.group("kind"),
            "signature": signature,
            "module": path.relative_to(ROOT).as_posix(),
            "line": line_no,
            "documentation": " ".join(piece for piece in comments if piece),
        })
        comments = []
    return entries


def render(entries: list[dict[str, object]]) -> str:
    out = [
        "# PunPun first-party API reference",
        "",
        "Generated from the checked-in PunPun standard library and first-party package sources.",
        "Run `pp doc` to regenerate or `pp doc --check` in CI.",
        "",
    ]
    by_module: dict[str, list[dict[str, object]]] = {}
    for entry in entries:
        by_module.setdefault(str(entry["module"]), []).append(entry)
    for module in sorted(by_module):
        out += [f"## `{module}`", ""]
        for entry in by_module[module]:
            out += [f"### `{entry['name']}`", "", "```punpun", str(entry["signature"]), "```", ""]
            if entry["documentation"]:
                out += [str(entry["documentation"]), ""]
    return "\n".join(out).rstrip() + "\n"


def build() -> tuple[str, str]:
    entries = [entry for path in source_files() for entry in extract(path)]
    data = {"format": 1, "version": (ROOT / "VERSION").read_text().strip(), "entries": entries}
    return render(entries), json.dumps(data, indent=2, sort_keys=True) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="generate first-party PunPun API docs")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    md, js = build()
    md_path = ROOT / "docs" / "api" / "REFERENCE.md"
    json_path = ROOT / "docs" / "api" / "index.json"
    site_path = ROOT / "docs-site" / "content" / "api-reference.md"
    if args.check:
        mismatches = []
        if not md_path.is_file() or md_path.read_text(encoding="utf-8") != md:
            mismatches.append(str(md_path.relative_to(ROOT)))
        if not json_path.is_file() or json_path.read_text(encoding="utf-8") != js:
            mismatches.append(str(json_path.relative_to(ROOT)))
        if not site_path.is_file() or site_path.read_text(encoding="utf-8") != md:
            mismatches.append(str(site_path.relative_to(ROOT)))
        if mismatches:
            raise SystemExit("generated API docs are stale: " + ", ".join(mismatches) + "; run `pp doc`")
        print("generated API docs are current")
        return 0
    md_path.parent.mkdir(parents=True, exist_ok=True)
    md_path.write_text(md, encoding="utf-8")
    json_path.write_text(js, encoding="utf-8")
    site_path.parent.mkdir(parents=True, exist_ok=True)
    site_path.write_text(md, encoding="utf-8")
    print(f"generated {md_path.relative_to(ROOT)} ({len(json.loads(js)['entries'])} entries)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
