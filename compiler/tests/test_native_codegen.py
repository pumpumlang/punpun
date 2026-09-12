#!/usr/bin/env python3
"""Native backend structural regressions.

The ordinary suite proves behavior.  These checks pin native-specific codegen
features that could otherwise regress while still producing correct output on
small programs: stack arguments, register allocation, and async trampolines.
"""
from __future__ import annotations

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PPC = os.environ.get("PPC", os.path.join(ROOT, "build", "ppc"))
STDLIB = os.path.join(ROOT, "stdlib")
CASES = os.path.join(ROOT, "compiler", "tests", "cases")


def assembly(case: str) -> str:
    proc = subprocess.run(
        [PPC, "emit-c", "--no-color", "--backend=native", "--stdlib", STDLIB,
         os.path.join(CASES, case)],
        capture_output=True, text=True, check=False,
    )
    if proc.returncode:
        raise AssertionError(proc.stderr.strip() or f"failed to emit {case}")
    return proc.stdout


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    wide = assembly("wide_calls.pp")
    require("pushq\t%rax" in wide,
            "wide calls no longer place overflow arguments on the native stack")
    require(re.search(r"%r(?:bx|1[2-5])", wide) is not None,
            "native linear-scan allocation no longer uses callee-saved registers")
    require("the native backend supports at most 6" not in wide,
            "obsolete wide-call diagnostic leaked into generated output")

    gui = assembly("gui_builtin_wide.pp")
    require("pp_gui_canvas_rect" in gui and "pushq\t%rax" in gui,
            "wide runtime builtins no longer spill overflow arguments on the native stack")

    async_asm = assembly("async_native.pp")
    require("pptask_spawn" in async_asm and "pp_task_spawn" in async_asm,
            "native async task wrapper/trampoline was not emitted")
    require("pp_task_await_i64" in async_asm and "pp_task_await_f64" in async_asm,
            "native await helpers are missing")

    escape = assembly("escape_analysis.pp")
    require("ppcopy" in escape,
            "escape-analysis fixture unexpectedly stopped exercising value copies")

    print("native codegen structural checks: ok")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as exc:
        print(f"native codegen structural check failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
