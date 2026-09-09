#!/usr/bin/env python3
"""Emit a deterministic source-location map from PunPun's direct x86 assembly.

The native backend already owns `.file`/`.loc` directives. This tool exposes
those directives as stable JSON for editor/debugger integrations instead of
inventing a second source mapping model.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

FILE_RE = re.compile(r'^\s*\.file\s+(\d+)\s+"(.*)"\s*$')
LOC_RE = re.compile(r'^\s*\.loc\s+(\d+)\s+(\d+)(?:\s+(\d+))?')
LABEL_RE = re.compile(r'^(pp_fn_[A-Za-z0-9_.$]+):\s*$')


def main() -> int:
    parser = argparse.ArgumentParser(description="emit PunPun source debug map")
    parser.add_argument("source")
    parser.add_argument("-o", "--output")
    parser.add_argument("--ppc", default=str(Path(__file__).resolve().parents[1] / "build" / "ppc"))
    args = parser.parse_args()

    source = Path(args.source).resolve()
    with tempfile.NamedTemporaryFile(suffix=".s") as tmp:
        proc = subprocess.run([args.ppc, "emit-asm", str(source), "-o", tmp.name], text=True, capture_output=True)
        if proc.returncode != 0:
            raise SystemExit(proc.stderr or proc.stdout or "ppc emit-asm failed")
        assembly = Path(tmp.name).read_text(encoding="utf-8")

    files: dict[int, str] = {}
    function = ""
    entries: list[dict[str, object]] = []
    seen: set[tuple[str, str, int, int]] = set()
    for raw in assembly.splitlines():
        if match := FILE_RE.match(raw):
            files[int(match.group(1))] = match.group(2).replace('\\"', '"').replace('\\\\', '\\')
            continue
        if match := LABEL_RE.match(raw):
            function = match.group(1)
            continue
        if match := LOC_RE.match(raw):
            file_id, line = int(match.group(1)), int(match.group(2))
            column = int(match.group(3) or 0)
            file = files.get(file_id, f"<file:{file_id}>")
            key = (function, file, line, column)
            if key in seen:
                continue
            seen.add(key)
            entries.append({"function": function, "file": file, "line": line, "column": column})

    payload = {
        "format": 1,
        "source": str(source),
        "entries": entries,
    }
    rendered = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    if args.output:
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8")
        print(f"debug map -> {output}")
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
