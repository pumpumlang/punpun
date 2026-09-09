# PunPun 1.x compatibility guarantees

1. Valid stable 1.0 source remains valid in compatible 1.x compilers unless it relied on explicitly unsafe/undefined behavior.
2. Frozen builtins/stdlib signatures cannot be removed or changed during 1.x; additions are allowed.
3. ABI/runtime/package/lockfile epoch values remain 1 during 1.x.
4. Deprecation requires documentation and a migration window; removal requires a new major line.
5. Stable CLI invocation forms remain accepted throughout 1.x.
