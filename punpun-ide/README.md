# PunPun IDE (MVP)

A small desktop IDE for PunPun 1.3 plus C, C++, headers and Markdown. This is intentionally an MVP, not a second VS Code.

## Included now

- PunPun `.pp`, C/C++/header, Markdown and plain-text editing
- clean Catppuccin-based dark interface using the curated PunPun IDE asset pack
- project explorer, tabs, outline, Problems, Output and Terminal panels
- Run for PunPun through PPC 1.3, and C/C++ through an installed compiler
- Debug through `pp debug` when available, otherwise PPC `-g` plus GDB/LLDB
- PPC language-service connection (`ppc serve --stdio`) for real PunPun diagnostics, hover and semantic completion
- local structural hints and document outline when the compiler is unavailable
- Markdown live preview
- Toolchain Manager that finds the latest GitHub release and privately installs Windows/Linux x86-64 builds under `~/.punpun-ide`

PPC remains the source of truth for PunPun semantics. The IDE does not maintain a second PunPun parser.

## Run from source

```sh
cd punpun-ide
python -m venv .venv
# Linux/macOS
. .venv/bin/activate
# Windows PowerShell: .venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
python run_ide.py ..
```

The IDE first looks for its private toolchain, then `ppc`, `pp`, or `punpun` on PATH. The **PunPun Toolchain** toolbar button can download/update the current public release.

## Keyboard

- `Ctrl+S`: save
- `F5`: run
- `F6`: debug
- `Ctrl+Shift+B`: check current file
- `Ctrl+Space`: PunPun semantic completion
- `F1`: PunPun semantic hover/help

## Build a desktop bundle

Install PyInstaller, then:

```sh
python -m pip install pyinstaller
python scripts/build_app.py
```

The output is written to `dist/`. CI packaging can use the same script on Windows and Linux.

## Scope

This first version deliberately avoids a plugin system, Git porcelain, project refactors and a custom debugger. PunPun 1.3 already exposes semantic diagnostics, hover, document symbols and completion over LSP; later IDE work should consume those compiler services rather than duplicate them.

## Third-party assets

The bundled toolbar SVGs come from Microsoft VS Code Codicons and Tabler. The color palette is Catppuccin. PunPun branding is copied from the supplied PunPun IDE asset pack. Corresponding license texts are in `assets/licenses/`.
