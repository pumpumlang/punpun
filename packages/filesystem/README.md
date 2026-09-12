# filesystem

First-party filesystem helpers backed by `std.filesystem` and `std.path`.

```sh
ppx add filesystem
```

Alongside the compatibility read/write helpers, the module supports path
normalization, metadata, recursive copy/remove/walk, line I/O, temporary names
and atomic text replacement. OS file primitives remain runtime calls; recursive
operations and path policy are PunPun code.
