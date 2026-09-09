# Windows installer source

This is the **real WiX v6 build project** for the PunPun MSI and graphical Burn setup EXE. The Linux release host cannot build or execute WiX/MSI artifacts, and no Windows compiler payload is present here, so this source is shipped rather than a fraudulent renamed ZIP.

On Windows x64:

1. Build/stage the PunPun Windows SDK into `payload/` (including `bin/pp.exe`, `bin/ppc.exe`, `bin/ppx.exe`/launcher, runtime, stdlib, VSIX and assets).
2. Install WiX Toolset v6 and the BootstrapperApplications extension.
3. Run `./build.ps1`.
4. Validate install, `pp --version`, Hello World, upgrade and uninstall in Windows CI/VM.

`build.ps1` reads the repository-root `VERSION` and derives the MSI-compatible numeric version. Output names therefore follow the same version as every other release artifact.

`PunPun.wxs` owns PATH and `.pp` association through Windows Installer so uninstall/upgrade are reversible. `Bundle.wxs` produces the friendly graphical bootstrapper.

`wix/Payload.wxs` uses WiX's recursive `Files` element, so the current SDK
payload is included without relying on the removed legacy harvesting command.

## `.pp` file icon association

The MSI registers `.pp` as `PunPun.Source` and sets its `DefaultIcon` to
`[INSTALLFOLDER]assets\punpun-source.ico`. The ICO is a multi-resolution asset
built from the canonical PunPun icon (16 through 256 px). The association is
owned by the MSI component so uninstall and upgrade can reverse/update it.
