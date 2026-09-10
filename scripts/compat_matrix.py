#!/usr/bin/env python3
"""Run the same PunPun programs across available backends and compare behavior."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CASES = [
    ROOT / "tests" / "fixtures" / "language_0_6" / "generics_run.pp",
    ROOT / "tests" / "fixtures" / "language_0_6" / "enums_run.pp",
    ROOT / "tests" / "fixtures" / "language_0_6" / "slices_run.pp",
    ROOT / "tests" / "fixtures" / "language_0_8" / "task_group_run.pp",
]


def run(ppc: str, case: Path, flags: list[str]) -> tuple[int, str, str]:
    result = subprocess.run([ppc, "run", str(case), "--no-cache", *flags], cwd=ROOT,
                            text=True, capture_output=True, timeout=60)
    return result.returncode, result.stdout, result.stderr


def main() -> int:
    parser = argparse.ArgumentParser(description="PunPun backend compatibility matrix")
    parser.add_argument("--ppc", default=str(ROOT / "build" / "ppc"))
    parser.add_argument("--quick", action="store_true")
    args = parser.parse_args()
    backends = [
        ("portable-c", ["--backend=c"]),
        ("native", ["--backend=native"]),
        ("bytecode", ["--backend=bytecode"]),
    ]
    cases = CASES[:2] if args.quick else CASES
    total = 0
    for case in cases:
        baseline = None
        case_backends = backends
        if "task_group" in case.name:
            # Native x86-64 diagnoses async as E1000; C and bytecode cover it.
            case_backends = [item for item in backends if item[0] != "native"]
        for name, flags in case_backends:
            result = run(args.ppc, case, flags)
            if result[0] != 0:
                raise SystemExit(f"{case.name} failed on {name}:\n{result[2]}")
            observable = (result[0], result[1])
            if baseline is None:
                baseline = observable
            elif observable != baseline:
                raise SystemExit(
                    f"backend mismatch for {case.name}: direct={baseline!r}, {name}={observable!r}"
                )
            total += 1
    print(f"compat matrix: {len(cases)} program(s), {total} backend passes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
