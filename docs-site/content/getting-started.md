# Getting Started

Get from an installed SDK to a running project without learning the whole language at once.

## Overview

For everyday development you mainly need three commands: `pp check`, `pp run`, and `pp build`. `pp new` creates the project structure for you.

## Syntax

Create a project and enter it:

```sh
pp new hello
cd hello
```

The generated `src/main.pp` contains a `launch` block. Replace it with:

```punpun
launch {
    say("My first PunPun project");
}
```

## Runnable example

Use this complete `src/main.pp`:

```punpun
launch {
    say("My first PunPun project");
}
```

Check the project without producing a final executable:

```sh
pp check
```

Run it:

```sh
pp run
```

Build a release executable when you need one:

```sh
pp build --release
```

Before debugging a setup problem, run `pp doctor`.

## Common mistakes

- `pp` is not found: open a new terminal after installing, or add the installer-reported bin directory to `PATH`.
- Running commands one directory above the project: enter the folder containing `Punpun.toml`.
- Editing generated/cache files under `.punpun`: edit `src/` instead.

## Next steps

Read [Language Basics](language.html) for variables, values, imports, and the `launch` entry block. Editor setup and OS file icons are documented in [Editor and File Icons](editor-icons.html).
