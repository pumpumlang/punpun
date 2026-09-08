# Learn PunPun

PunPun is an ahead-of-time native language with a small everyday workflow: write `.pp`, check it, then build or run a native executable.

## Overview

Start here if this is your first PunPun program. The learning path is ordered so each page uses only ideas introduced earlier.

- Beginner: Getting Started → Language Basics → Functions → Control Flow.
- Intermediate: Objects and Structs → Generics and Results → Packages and PPX → Native Memory.
- Advanced: Async, Toolchains, Foreign Injection, Performance, and Self-hosting.

## Syntax

A PunPun program normally begins in a `launch` block. Statements end with semicolons and blocks use braces.

```punpun
launch {
    say("Hello from PunPun!");
}
```

## Runnable example

Save this as `hello.pp`:

```punpun
launch {
    say("Hello from PunPun!");
}
```

Then run:

```sh
pp run hello.pp
```

You should see `Hello from PunPun!`.

## Common mistakes

- Writing `launch:` / `done` from old syntax. Current PunPun uses braces.
- Forgetting the semicolon after an ordinary statement.
- Running `pp build` from the wrong folder when you meant to run one file. Use `pp run hello.pp` for a standalone file.

## Next steps

Continue to [Getting Started](getting-started.html) to install the SDK, create a project, and learn the three commands you will use most.
