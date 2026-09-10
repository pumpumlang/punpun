# PunPun for Visual Studio Code

Official `.pp` support for PunPun 1.3. The extension launches PPC's built-in
language server; it does not bundle a second parser or the former Node bridge.

## Implemented features

- live compiler diagnostics with stable error codes and related locations;
- semantic completion;
- hover information;
- go to definition where the compiler index has a target;
- nested document symbols;
- syntax highlighting and PunPun file icons;
- build, run, check, test, and language-server restart commands.

References, rename, formatting, signature help, code actions, inlay hints,
semantic tokens, and workspace symbols are not advertised until PPC implements
them.

## Install

```sh
./punpun editor install-vscode
```

Release builds also contain a versioned VSIX. The extension discovers
workspace-local or installed `ppc`; `punpun.compilerPath` can select an
explicit compiler. `punpun.toolPath` can select the `pp` launcher used by
build commands.

Language id: `punpun`; grammar scope: `source.punpun`; extension: `.pp`.
