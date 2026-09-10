# Native Toolchains

Inspect available tools:

```sh
pp toolchain detect
pp toolchain list
pp toolchain info clang
```

Select a supported GNU-like driver:

```sh
pp build --cc clang
pp build --cc gcc
```

Toolchain identity is part of the runtime-object cache fingerprint. The driver
must accept GCC-compatible compile/link flags. Windows support is qualified by
its real workflow rather than inferred from Linux.
