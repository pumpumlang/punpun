#!/usr/bin/env python3
"""Deterministic mutation fuzzer for the PunPun frontend/semantic pipeline."""
from __future__ import annotations

import argparse
from pathlib import Path
import random
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TOKENS = ["{", "}", "(", ")", ";", "let ", "fn ", "launch ", "?", "::", "&mut ", "match ", "return ", "0", "true", '"x"']


def mutate(source: str, rng: random.Random) -> str:
    if not source:
        return "launch { }\n"
    mode = rng.randrange(4)
    pos = rng.randrange(len(source) + 1)
    if mode == 0:
        return source[:pos] + rng.choice(TOKENS) + source[pos:]
    if mode == 1 and len(source) > 1:
        end = min(len(source), pos + rng.randint(1, 12))
        return source[:pos] + source[end:]
    if mode == 2 and pos < len(source):
        return source[:pos] + rng.choice("{}()[];,+-*?!&|0129xyz") + source[pos + 1:]
    left = max(0, pos - rng.randint(0, 8))
    right = min(len(source), pos + rng.randint(0, 8))
    return source[:pos] + source[left:right] + source[pos:]


def main() -> int:
    parser = argparse.ArgumentParser(description="fuzz PunPun parser/type/ownership pipeline")
    parser.add_argument("--iterations", type=int, default=100)
    parser.add_argument("--seed", type=int, default=0x5050)
    parser.add_argument("--ppc", default=str(ROOT / "build" / "ppc"))
    args = parser.parse_args()
    if args.iterations <= 0:
        raise SystemExit("iterations must be positive")
    corpus = [p.read_text(encoding="utf-8") for p in sorted((ROOT / "tests" / "fuzz_corpus").glob("*.pp"))]
    if not corpus:
        raise SystemExit("fuzz corpus is empty")
    rng = random.Random(args.seed)
    with tempfile.TemporaryDirectory(prefix="punpun-fuzz-") as td:
        path = Path(td) / "case.pp"
        for index in range(args.iterations):
            source = mutate(rng.choice(corpus), rng)
            path.write_text(source, encoding="utf-8")
            try:
                result = subprocess.run([args.ppc, "check", str(path), "--no-cache"], text=True,
                                        capture_output=True, timeout=4)
            except subprocess.TimeoutExpired:
                raise SystemExit(f"fuzz case {index} timed out (seed={args.seed})")
            combined = result.stdout + result.stderr
            if result.returncode < 0:
                raise SystemExit(f"fuzz case {index} crashed by signal {-result.returncode} (seed={args.seed})")
            if "internal compiler error" in combined.lower() or "terminate called" in combined.lower():
                reproduction = ROOT / ".punpun" / "fuzz-repro.pp"
                reproduction.parent.mkdir(parents=True, exist_ok=True)
                reproduction.write_text(source, encoding="utf-8")
                raise SystemExit(f"fuzz case {index} reached an internal compiler failure; repro -> {reproduction}")
    print(f"fuzz: {args.iterations} mutation cases passed (seed={args.seed})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
