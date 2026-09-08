# Getting Started

## Install on Linux

Use the portable SDK or the self-extracting installer. Arch/CachyOS users can also install the generated pacman package with `sudo pacman -U`.

After installation:

```sh
pp --version
pp doctor
pp new demo
cd demo
pp run
```

## VS Code

Install the bundled `punpun-vscode-<VERSION>.vsix`, reload the editor, and open a `.pp` file. Unknown identifiers are diagnosed from unsaved buffers without building machine code.
