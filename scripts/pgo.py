#!/usr/bin/env python3
"""Train a stable portable-C PGO build for a PunPun program.

This deliberately uses PunPun's shared frontend + C backend and a stable C/object
path so GCC/Clang profile files can be reused between the instrumented and
optimized compilation. Direct x86-64 PGO remains a later backend project.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def checked(command: list[str], **kwargs) -> None:
    result = subprocess.run(command, text=True, capture_output=True, **kwargs)
    if result.returncode != 0:
        raise SystemExit("command failed: " + " ".join(command) + "\n" + result.stdout + result.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description="PunPun portable-C profile-guided optimization")
    parser.add_argument("source")
    parser.add_argument("-o", "--output")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    import sys
    argv = sys.argv[1:]
    training_args: list[str] = []
    if "--" in argv:
        separator = argv.index("--")
        training_args = argv[separator + 1:]
        argv = argv[:separator]
    args = parser.parse_args(argv)
    if not shutil.which(args.cc):
        raise SystemExit(f"compiler not found: {args.cc}")
    source = Path(args.source).resolve()
    if "@inject->" in source.read_text(encoding="utf-8"):
        raise SystemExit("pgo helper currently requires a program without @inject blocks")
    work = ROOT / ".punpun" / "pgo" / source.stem
    profile = work / "profiles"
    work.mkdir(parents=True, exist_ok=True)
    profile.mkdir(parents=True, exist_ok=True)
    c_file = work / "program.c"
    obj = work / "program.o"
    train = work / "train"
    output = Path(args.output).resolve() if args.output else (ROOT / ".punpun" / "bin" / f"{source.stem}-pgo")
    output.parent.mkdir(parents=True, exist_ok=True)
    ppc = ROOT / "build" / "ppc"
    checked([str(ppc), "emit-c", str(source), "-o", str(c_file)], cwd=ROOT)
    common = ["-std=c17", "-O3", "-I" + str(ROOT / "runtime")]
    checked([args.cc, *common, f"-fprofile-generate={profile}", "-c", str(c_file), "-o", str(obj)], cwd=ROOT)
    checked([args.cc, f"-fprofile-generate={profile}", str(obj), str(ROOT / "runtime" / "libpunpun.a"), "-lm", "-pthread", "-o", str(train)], cwd=ROOT)
    checked([str(train), *training_args], cwd=source.parent)
    checked([args.cc, *common, f"-fprofile-use={profile}", "-fprofile-correction", "-c", str(c_file), "-o", str(obj)], cwd=ROOT)
    checked([args.cc, f"-fprofile-use={profile}", "-fprofile-correction", str(obj), str(ROOT / "runtime" / "libpunpun.a"), "-lm", "-pthread", "-o", str(output)], cwd=ROOT)
    print(f"PGO optimized executable -> {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
