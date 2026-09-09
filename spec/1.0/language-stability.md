# PunPun 1.0 language stability contract

PunPun 1.0 freezes source-language compatibility at **1.0**. The 1.x line follows semantic versioning: compatible releases may add APIs and syntax but may not remove stable keywords, change stable builtin signatures, remove stable standard-library signatures, or reinterpret previously valid 1.0 source incompatibly.

The machine-readable baseline is `stable-api.json`, enforced by `pp stable-check`. Breaking changes require a future major language epoch. Diagnostics wording and optimizer decisions are not compatibility promises unless explicitly specified.
