#!/usr/bin/env python3
"""Generate repeatable multi-module PunPun compiler benchmarks.

The benchmark uses deterministic source, preserves raw samples, and records
cold build, warm build, check, and one-function incremental-edit timings. It
does not print competitive claims; compare JSON files from equivalent hosts.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
PP = ROOT / "pp"


def run_timed(command: list[str], cwd: Path) -> dict:
    started = time.perf_counter_ns()
    process = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, timeout=600)
    elapsed = (time.perf_counter_ns() - started) / 1_000_000.0
    if process.returncode != 0:
        raise RuntimeError(f"command failed: {command}\n{process.stdout}\n{process.stderr}")
    return {"milliseconds": elapsed, "stdout": process.stdout, "stderr": process.stderr}


def generate(root: Path, count: int) -> Path:
    project = root / f"modules-{count}"
    source = project / "src"
    source.mkdir(parents=True)
    (project / "Punpun.toml").write_text(
        '[package]\nname = "benchmark"\nversion = "0.1.0"\nentry = "src/main.pp"\n',
        encoding="utf-8",
    )
    for index in range(count):
        following = f"bring module_{index + 1};\n" if index + 1 < count else ""
        expression = f"value_{index + 1}() + 1" if index + 1 < count else "1"
        (source / f"module_{index}.pp").write_text(
            f"{following}fn value_{index}() -> i64 {{ return {expression}; }}\n",
            encoding="utf-8",
        )
    (source / "main.pp").write_text(
        'bring module_0;\nlaunch { say(value_0()); }\n', encoding="utf-8")
    return project


def samples(command: list[str], cwd: Path, rounds: int) -> list[float]:
    return [run_timed(command, cwd)["milliseconds"] for _ in range(rounds)]


def summarize(values: list[float]) -> dict:
    return {
        "median_ms": statistics.median(values),
        "minimum_ms": min(values),
        "maximum_ms": max(values),
        "samples_ms": values,
    }


def benchmark(project: Path, count: int, rounds: int) -> dict:
    check = samples([str(PP), "check"], project, rounds)
    subprocess.run([str(PP), "clean"], cwd=project, check=True, stdout=subprocess.DEVNULL)
    cold = [run_timed([str(PP), "build", "--release"], project)["milliseconds"]]
    warm = samples([str(PP), "build", "--release"], project, rounds)

    changed = project / "src" / f"module_{count - 1}.pp"
    changed.write_text(f"fn value_{count - 1}() -> i64 {{ return 2; }}\n", encoding="utf-8")
    incremental = run_timed([str(PP), "build", "--release", "--stats"], project)
    executable = project / ".punpun" / "bin" / "main"
    executed = subprocess.run([str(executable)], cwd=project, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    expected = str(count + 1)
    if executed.returncode != 0 or executed.stdout.strip() != expected:
        raise RuntimeError(f"benchmark executable returned {executed.stdout!r}, expected {expected!r}")
    return {
        "module_count": count,
        "check": summarize(check),
        "cold_release_build": summarize(cold),
        "warm_release_build": summarize(warm),
        "one_function_edit_build": {
            "milliseconds": incremental["milliseconds"],
            "compiler_stats": incremental["stderr"].splitlines(),
        },
        "verified_output": expected,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--modules", nargs="+", type=int, default=[10, 100, 500, 1000])
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--output", type=Path, default=ROOT / "benchmarks" / "latest.json")
    args = parser.parse_args()
    if args.rounds < 1 or any(value < 1 for value in args.modules):
        parser.error("module counts and rounds must be positive")
    with tempfile.TemporaryDirectory(prefix="punpun-large-project-") as temporary:
        workspace = Path(temporary)
        results = [benchmark(generate(workspace, count), count, args.rounds) for count in args.modules]
    payload = {
        "schema": 1,
        "compiler": subprocess.run([str(PP), "--version"], text=True, capture_output=True,
                                   check=True).stdout.strip(),
        "platform": os.uname().sysname + " " + os.uname().machine if hasattr(os, "uname") else os.name,
        "results": results,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(args.output)


if __name__ == "__main__":
    main()
