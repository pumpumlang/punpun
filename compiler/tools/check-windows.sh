#!/usr/bin/env bash
# Verifies the Windows platform layer actually compiles.
#
# WHY THIS EXISTS
#
# This source-level gate catches Windows API, calling-convention, and link
# mistakes before the real Windows workflow runs. It does not replace execution
# on Windows.
#
# USAGE
#
#   tools/check-windows.sh                 # cross-compile with mingw-w64
#   CC=cl tools/check-windows.sh           # native MSVC
#
# On Debian/Ubuntu the cross-compiler is:
#   sudo apt install gcc-mingw-w64-x86-64
#
# WHAT THIS DOES AND DOES NOT PROVE
#
# Passing means the Windows source compiles and links, and that the guarded
# POSIX file correctly reduces to nothing. It does NOT prove the runtime behaves
# correctly on Windows: threads, cancellation timing, and console encoding all
# need a real Windows machine running `make test`. Compilation is the floor, not
# the ceiling.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CROSS="${CROSS:-x86_64-w64-mingw32-gcc}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

if ! command -v "$CROSS" >/dev/null 2>&1; then
    echo "error: $CROSS not found."
    echo
    echo "  Debian/Ubuntu:  sudo apt install gcc-mingw-w64-x86-64"
    echo "  Fedora:         sudo dnf install mingw64-gcc"
    echo "  macOS:          brew install mingw-w64"
    echo
    echo "Or set CROSS to your Windows C compiler."
    exit 1
fi

echo "Windows platform layer check"
echo "  compiler: $CROSS ($($CROSS --version | head -1))"
echo

failures=0

compile() {
    local source="$1"
    local name
    name="$(basename "$source")"
    printf '  %-32s ' "$name"
    if "$CROSS" -std=c11 -Wall -Wextra -O2 -c "$source" \
            -I "$ROOT/runtime" -o "$OUT/$name.obj" 2> "$OUT/$name.log"; then
        if [ -s "$OUT/$name.log" ]; then
            echo "ok (with warnings)"
            sed 's/^/      /' "$OUT/$name.log"
        else
            echo "ok"
        fi
    else
        echo "FAILED"
        sed 's/^/      /' "$OUT/$name.log"
        failures=$((failures + 1))
    fi
}

echo "Compiling the runtime for Windows:"
compile "$ROOT/runtime/ppc_platform_windows.c"
compile "$ROOT/runtime/ppc_platform_posix.c"   # must reduce to an empty TU
compile "$ROOT/runtime/ppcrt.c"
compile "$ROOT/runtime/ppc_https.c"
compile "$ROOT/runtime/ppc_gui.c"

echo
echo "Linking a program against the Windows runtime:"
cat > "$OUT/smoke.c" <<'SMOKE'
/* Exercises the pieces most likely to be wrong on a first Windows port:
 * threads, mutexes, thread-local storage, the monotonic clock, and sleeping. */
#include "ppcrt.h"
#include <stdio.h>

static uintptr_t worker(void *context) {
    (void)context;
    pp_sleep_ms(20);
    return 42;
}

int main(int argc, char **argv) {
    pp_runtime_init(argc, argv);

    const int64_t started = pp_clock_ms();
    pp_task *task = pp_task_spawn(worker, NULL, 0);
    const int64_t result = pp_task_await_i64(task);
    const int64_t elapsed = pp_clock_ms() - started;

    const int64_t group = pp_task_group_new();
    pp_task_group_close(group);

    printf("result=%lld elapsed_ok=%d\n",
           (long long)result, elapsed >= 15 && elapsed < 5000);
    pp_println_str("runtime smoke ok");
    pp_runtime_cleanup();
    return result == 42 ? 0 : 1;
}
SMOKE

printf '  %-32s ' "smoke.exe"
if "$CROSS" -std=c11 -O2 "$OUT/smoke.c" \
        "$OUT/ppcrt.c.obj" "$OUT/ppc_platform_windows.c.obj" \
        "$OUT/ppc_platform_posix.c.obj" "$OUT/ppc_https.c.obj" \
        "$OUT/ppc_gui.c.obj" -luser32 \
        -I "$ROOT/runtime" -o "$OUT/smoke.exe" 2> "$OUT/link.log"; then
    echo "ok"
else
    echo "FAILED"
    sed 's/^/      /' "$OUT/link.log"
    failures=$((failures + 1))
fi

echo
if [ "$failures" -eq 0 ]; then
    echo "PASS: the Windows runtime compiles and links."
    echo
    echo "This does not prove it runs correctly. Next step: copy the tree to a"
    echo "Windows machine, build with 'make CC=gcc CXX=g++', and run 'make test'."
    if command -v wine >/dev/null 2>&1; then
        echo
        echo "wine is available; running the smoke test:"
        wine "$OUT/smoke.exe" 2>/dev/null | sed 's/^/  /' || echo "  (wine run failed)"
    fi
    exit 0
fi

echo "FAIL: $failures compilation unit(s) failed."
exit 1
