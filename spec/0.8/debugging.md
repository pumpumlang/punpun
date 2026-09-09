# PunPun 0.8 source-debugging contract

The direct x86-64 backend emits assembler source directives (`.file` and `.loc`) for PunPun source locations. `pp debug-map` converts these directives into deterministic JSON entries containing PunPun function, file, line and column information.

`pp debug` builds a debug-oriented executable and delegates interactive stepping/inspection to an installed GDB or LLDB. PunPun does not claim a custom debugger protocol in 0.8.

Committed/generated debug metadata must not embed account-specific repository paths. Reproducible source mapping is part of the debugging contract.
