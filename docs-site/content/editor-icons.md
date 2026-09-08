# Editor and File Icons

PunPun uses the same canonical mark for `.pp` files in VS Code, Linux file managers, and Windows Explorer.

## Overview

The repository keeps source artwork under `assets/`. Platform-specific association files point back to those assets instead of maintaining unrelated logos.

## Syntax

VS Code associates `.pp` with the `punpun` language and contributes light/dark language icons from:

```text
editors/vscode/assets/punpun-file-light.png
editors/vscode/assets/punpun-file-dark.png
```

Linux uses MIME type `application/x-punpun` from:

```text
packaging/linux/application-x-punpun.xml
```

Windows uses the MSI ProgID `PunPun.Source` and:

```text
assets/punpun-source.ico
```

## Runnable example

Install or refresh the Linux association for your user account:

```sh
./packaging/linux/install-file-icons.sh
```

A system package can use:

```sh
sudo ./packaging/linux/install-file-icons.sh --system
```

Then reopen the file manager if it cached the old icon. The normal PunPun Linux installer performs the per-user registration automatically.

## Common mistakes

- Renaming a PNG to `.ico`. Windows Explorer expects a real icon container, so PunPun ships a multi-resolution ICO.
- Expecting VS Code to ignore the active file-icon theme. PunPun contributes a language icon, but a theme with its own explicit `.pp` mapping can intentionally override it.
- Copying MIME XML without refreshing the shared MIME database. The install script runs `update-mime-database` when available.

## Next steps

Read [Getting Started](getting-started.html) to install the SDK or [Compiler Errors](errors.html) to configure the editor around compiler-backed diagnostics.
