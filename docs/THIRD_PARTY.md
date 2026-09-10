# Third-party dependencies and integration

PunPun's core compiler is C++ and the runtime is C. The release does not bundle external compilers, TLS stacks, GUI servers, or package-manager runtimes without need.

- **System C/C++ toolchain / linker**: used for final assembly/linking and explicit native integration. The SDK detects compatible installed tools; licensing belongs to the selected toolchain distribution.
- **libcurl**: the 1.3 runtime, `std.net.https`, and the first-party HTTPS/requests packages dynamically load the system libcurl. Certificate and hostname verification are explicitly enabled and redirects are restricted to HTTPS. PunPun does not ship a TLS implementation.
- **X11/XWayland**: the POSIX GUI backend dynamically loads system X11 when a graphical display is available. Headless systems and Wayland sessions without XWayland remain supported; GUI availability simply reports false.
- **Win32**: the Windows GUI backend uses the operating system's User32 message-box API.
- **Python 3**: currently required by the PPX client/registry and static website build scripts. It is not required by compiled PunPun applications or normal `pp build`/`pp run`.
- **VS Code**: optional editor host. Its extension launches PPC's built-in language server directly; Node.js is no longer a PunPun SDK/LSP dependency.
- **WiX Toolset v4**: required only to build the Windows MSI and graphical Burn bootstrapper from `installers/windows/`.

Consult the license terms supplied by your operating system/toolchain for these external components. PunPun itself is distributed under the repository `LICENSE`.
