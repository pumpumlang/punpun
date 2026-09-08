# Native Toolchains

Inspect available tools:

```sh
pp toolchain detect
pp toolchain list
pp toolchain info clang
```

Select a supported GNU-like driver:

```sh
pp build --toolchain clang
pp build --toolchain gcc --linker lld
```

Toolchain identity is part of the executable cache fingerprint. MSVC is capability-detected by tooling but its direct adapter is not claimed working in this Linux-built beta.
