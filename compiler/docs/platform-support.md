# Platform support

| platform | compiler | runtime | tested |
|---|---|---|---|
| Linux x86-64 | working | working | continuously — full suite |
| Windows x86-64 | driver code written | runtime code written | **never compiled or run** |
| macOS | expected to work | expected to work | never tested |

## Honest status of Windows

The code exists. It has never been through a compiler.

`runtime/ppc_platform_windows.c` and the Windows branches in
`src/support/host.cpp` were written in an environment with no Windows toolchain
and no way to install a cross-compiler. They have been reviewed, not tested.
**Do not describe Windows as supported.**

Run this before believing otherwise:

```sh
tools/check-windows.sh          # needs mingw-w64
```

Passing proves the Windows sources compile and link, and that the POSIX file
correctly reduces to nothing. It does **not** prove correct behaviour: threads,
cancellation timing, and console encoding need a real Windows machine running
`make test`.

## Structure

Two boundaries, for two different consumers:

- `runtime/ppc_platform.h` — what compiled PunPun programs need: threads,
  mutexes, thread-local storage, a monotonic clock, sleeping, and the handful of
  filesystem operations the `fs` builtins expose. Two implementations sit behind
  it, each guarded by a whole-file `#if`, so the build lists both and the
  preprocessor picks. `runtime/ppcrt.c` contains no `#ifdef` at all.
- `include/ppc/support/host.hpp` — what the compiler driver needs: executable
  path discovery, temporary files, subprocess execution, cache locations, and
  toolchain identification.

The scope rule for both: expose only capabilities that are actually called. An
abstraction full of unused entry points is a maintenance cost with no user, and
every unused function is one nobody notices is broken.

The naming rule: expose the capability, never the platform's spelling of it.
`ppc_plat_monotonic_ms` is right; `ppc_plat_clock_gettime` would not be, because
it names a POSIX function and would force the Windows side to emulate an
interface it does not have.

## Windows decisions worth knowing

- **Threads use `_beginthreadex`, not `CreateThread`.** CreateThread does not
  initialize the CRT's per-thread state, and the runtime calls CRT functions on
  worker threads. That would be a latent corruption bug, not an obvious failure.
- **`QueryPerformanceCounter`, not `GetTickCount64`.** GetTickCount64 has ~16 ms
  resolution, too coarse for what `clock_ms` is used to measure.
- **`MoveFileEx` with `MOVEFILE_REPLACE_EXISTING`.** Plain `MoveFile` fails when
  the destination exists; the runtime-object cache depends on rename-over being
  atomic, so the two platforms must agree.
- **`SetConsoleOutputCP(CP_UTF8)` at startup.** PunPun strings are UTF-8 by
  definition; without this, non-ASCII text prints as mojibake.

## What the POSIX refactor proves

The full suite (62 cases across three backends, plus the sanitizer build) passes
through the new abstraction, including concurrency and cancellation. That
demonstrates the refactor preserved behaviour on the platform that can be
tested. It says nothing about the platform that cannot.
