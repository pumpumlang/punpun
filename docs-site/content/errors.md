# Compiler Errors

PunPun diagnostics use stable category codes. For example `E0201` is an unknown name and carries an exact source span plus typo suggestion when scope information provides a close match.

```sh
pp explain E0201
```

VS Code consumes the same compiler diagnostics through the persistent semantic worker, so CLI and editor do not maintain competing semantic rules.
