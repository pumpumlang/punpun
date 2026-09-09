# Third-party dependencies and integration

PunPun's core compiler is C++ and the runtime is C. The release does not bundle external compilers, TLS stacks, GUI servers, or package-manager runtimes without need.

- **System C/C++ toolchain / linker**: used for final assembly/linking and explicit native integration. The SDK detects compatible installed tools; licensing belongs to the selected toolchain distribution.
- **libcurl**: the first-party `requests` package dynamically loads the system libcurl on Linux. Certificate verification remains libcurl's normal secure default. PunPun does not ship a homemade TLS implementation.
- **X11**: the Linux PunUI beta backend dynamically loads system X11 when a graphical display is available. Headless systems remain supported for non-GUI software.
- **Python 3**: currently required by the PPX client/registry and static website build scripts. It is not required by compiled PunPun applications or normal `pp build`/`pp run`.
- **Node.js**: currently required for the bundled language server bridge used by the VS Code extension. It is not required by compiled PunPun programs.
- **WiX Toolset v4**: required only to build the Windows MSI and graphical Burn bootstrapper from `installers/windows/`.

Consult the license terms supplied by your operating system/toolchain for these external components. PunPun itself is distributed under the repository `LICENSE`.
