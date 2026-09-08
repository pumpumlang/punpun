# Punpun for Visual Studio Code

VS Code support for Punpun 0.4 (`.pp`): syntax highlighting, snippets, autocomplete,
hover signatures, local-symbol suggestions, and compiler commands.

## Install from the repository

From the Punpun repository root on Linux:

```sh
./punpun editor install-vscode
```

That symlinks `editors/vscode` into `~/.vscode/extensions/punpun-local`. Reload VS
Code after installing. Because it is a symlink, edits to the extension in this
checkout are picked up after another reload.

You can also copy this folder to a VS Code extensions directory manually or package
it with `vsce package`.

## Completion support

Completions include:

- language blocks such as `launch`, `craft`, `shape`, `when`, `whilst`, and `each`;
- built-ins with argument snippets (`push`, `slice`, `assert`, `arg`, and the rest);
- standard-library functions from `std.math`, `std.stats`, `std.text`, `std.nums`,
  `std.time`, and `std.testing`, with automatic `bring` insertion when needed;
- `bring std...` module-name completion;
- `craft`, `shape`, `pin`, and `keep` names found in the current file;
- Punpun scalar/list types and boolean literals.

Hover a built-in or known stdlib function to see its signature/source module.

## Compiler commands

Open the command palette and run:

- **Punpun: Build Current File**
- **Punpun: Run Current File**
- **Punpun: Check Current File**
- **Punpun: Build Windows x86-64 .exe**

The extension first looks for workspace-local `./pp` / `./punpun` launchers, then
for the standard Linux installer path `~/.local/bin/pp`, then falls back to `pp` on
`PATH`. Set `punpun.toolPath` if you want a specific launcher.

The grammar scope is `source.punpun`.
