#!/usr/bin/env python3
"""Repeat compiler/runtime workloads to catch state leaks and flaky concurrency."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description="PunPun long-run stress harness")
    parser.add_argument("--iterations", type=int, default=50)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--ppc", default=str(ROOT / "build" / "ppc"))
    args = parser.parse_args()
    iterations = 5 if args.quick else args.iterations
    if iterations <= 0:
        raise SystemExit("iterations must be positive")
    case = ROOT / "tests" / "fixtures" / "language_0_8" / "task_group_run.pp"
    started = time.monotonic()
    for index in range(iterations):
        flags = [] if index % 2 == 0 else ["--cc-backend"]
        result = subprocess.run([args.ppc, "run", str(case), "--no-cache", *flags], cwd=ROOT,
                                text=True, capture_output=True, timeout=30)
        if result.returncode != 0 or result.stdout.strip().splitlines()[-1:] != ["42"]:
            raise SystemExit(f"stress iteration {index} failed:\n{result.stdout}\n{result.stderr}")
    elapsed = time.monotonic() - started
    print(f"stress: {iterations} async/compiler iterations passed in {elapsed:.3f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
