# Performance and profiling

PunPun treats compiler/runtime performance as something to measure, not a badge
to invent numbers for.

Use compiler timing and cache diagnostics:

```sh
pp build --timings
pp build --stats
pp build --cache-info
```

The direct native backend performs function-granular incremental object reuse.
Step 9 keeps the scalability gate in CI and adds backend compatibility, fuzz,
and long-run stress harnesses:

```sh
pp compat --quick
pp fuzz --iterations 100
pp stress --quick
```

## Portable-C profile-guided optimization

`pp pgo` exposes a reproducible PGO path through the shared PunPun frontend and
portable-C backend. It emits one stable C/object path, builds an instrumented
program, runs the training workload, then rebuilds with the collected profile:

```sh
pp pgo src/main.pp -o .punpun/bin/app-pgo -- --training-arg
```

This is intentionally not described as direct-x86 PGO. The project-owned x86
backend does not yet have its own profile instrumentation format.
