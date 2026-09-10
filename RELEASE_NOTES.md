# PunPun 1.3.0 release notes

PunPun 1.3 replaces the legacy compiler with the supplied PPC compiler while
keeping the stable PunPun 1.0 language, runtime ABI, package, and lockfile epochs.

## Compiler

- One C++20 frontend feeds portable C, direct x86-64, and register-bytecode backends.
- The compiler now exposes its own LSP server through `ppc serve --stdio`.
- Stable 1.0 keywords, builtins, and standard-library signatures remain machine checked.
- Common 1.0 CLI spellings remain accepted where they have a direct PPC equivalent.
- Compiler and runtime caches include the 1.3 compiler/cache identity.

## Libraries

- Added HTTPS primitives plus `std.net.https` and a first-party `https` package.
- TLS certificate and hostname verification are mandatory; redirects remain HTTPS-only.
- Added `std.gui` and runtime-native message windows using Win32 or dynamically loaded X11/XWayland.
- Expanded collections, data, math, system, and text modules.
- The existing `requests` package keeps its `HttpResponse` API but now shares the HTTPS runtime.

## Tooling and release

- VS Code talks directly to PPC and advertises only implemented LSP capabilities.
- Generated API documentation now includes nested standard-library modules.
- Cross-backend compiler regressions, LSP tests, ABI checks, privacy checks, and release validation use the new compiler/runtime layout.

Linux x86-64 is the locally validated host. Windows and Arch are not called
qualified until their actual GitHub workflows pass for the release candidate.
