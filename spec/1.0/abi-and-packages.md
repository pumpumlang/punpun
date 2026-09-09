# PunPun 1.0 ABI, package and lockfile policy

PunPun 1.x defines **language ABI 1**, **runtime ABI 1**, **package format 1**, and **Punpun.lock format 1**. `ppc language-info` reports all four. New projects declare `language = "1.0"` and `abi = 1`; incompatible requirements are rejected before compilation.

The public C runtime exposes `PUNPUN_RUNTIME_ABI_VERSION` and `pp_runtime_abi_version()`. Portable-C output rejects a mismatched runtime header. Format-1 lockfiles remain readable throughout 1.x.
