#!/usr/bin/env python3
"""Wire the new artwork into the VS Code extension manifest, idempotently.

Rewrites only the keys it owns and leaves everything else in
``editors/vscode/package.json`` untouched. Running it twice is a no-op.

    python3 scripts/patch_extension_manifest.py --repo-root .
    python3 scripts/patch_extension_manifest.py --repo-root . --check

``--check`` exits non-zero if the manifest is out of date, which makes it
usable as a CI gate.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ICON_THEME_ID = "punpun"
ICON_THEME_LABEL = "PunPun"
ICON_THEME_PATH = "./fileicons/punpun-icon-theme.json"

# Only these keys are managed here.
MANAGED = {
    "icon": "icon.png",
    "galleryBanner": {"color": "#11151e", "theme": "dark"},
}


def load(path: Path):
    raw = path.read_text(encoding="utf-8")
    try:
        return json.loads(raw), raw
    except json.JSONDecodeError as exc:
        raise SystemExit(f"patch_extension_manifest: {path} is not valid JSON: {exc}")


def patch(manifest: dict) -> tuple[dict, list[str]]:
    changes: list[str] = []

    for key, value in MANAGED.items():
        if manifest.get(key) != value:
            changes.append(f"set {key} = {json.dumps(value)}")
            manifest[key] = value

    contributes = manifest.setdefault("contributes", {})
    themes = contributes.setdefault("iconThemes", [])

    desired = {"id": ICON_THEME_ID, "label": ICON_THEME_LABEL, "path": ICON_THEME_PATH}
    for entry in themes:
        if entry.get("id") == ICON_THEME_ID:
            if entry != desired:
                changes.append(f"updated iconThemes[{ICON_THEME_ID}]")
                entry.clear()
                entry.update(desired)
            break
    else:
        changes.append(f"registered iconThemes[{ICON_THEME_ID}]")
        themes.append(desired)

    # The marketplace icon must ship inside the VSIX. If .vscodeignore-style
    # packaging lists exist, make sure the icon and fileicons survive.
    files = manifest.get("files")
    if isinstance(files, list):
        for needed in ("icon.png", "fileicons"):
            if needed not in files:
                changes.append(f"added {needed} to files[]")
                files.append(needed)

    return manifest, changes


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo-root", default=".", type=Path)
    ap.add_argument("--check", action="store_true",
                    help="report drift and exit 1 instead of writing")
    args = ap.parse_args()

    path = args.repo_root / "editors" / "vscode" / "package.json"
    if not path.is_file():
        print(f"patch_extension_manifest: no manifest at {path}", file=sys.stderr)
        return 2

    manifest, raw = load(path)
    manifest, changes = patch(manifest)

    if not changes:
        print("extension manifest already current")
        return 0

    if args.check:
        print("extension manifest is out of date:", file=sys.stderr)
        for c in changes:
            print(f"  - {c}", file=sys.stderr)
        return 1

    indent = 2
    for line in raw.splitlines():
        stripped = line.lstrip(" ")
        if stripped and stripped != line and stripped.startswith('"'):
            indent = len(line) - len(stripped)
            break

    path.write_text(json.dumps(manifest, indent=indent, ensure_ascii=False) + "\n",
                    encoding="utf-8")
    for c in changes:
        print(f"  - {c}")
    print(f"updated {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
