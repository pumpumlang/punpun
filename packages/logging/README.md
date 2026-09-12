# logging

First-party logging package backed by `std.logging`.

```sh
ppx add logging
```

It provides trace/debug/info/warn/error levels, named loggers, thresholds,
timestamps and optional file sinks. The package keeps the original convenience
functions while the reusable `Logger` implementation lives in PunPun.
