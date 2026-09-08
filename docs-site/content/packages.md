# Packages and PPX

PunPun uses one manifest model: `Punpun.toml` plus `Punpun.lock`. `pp` owns compilation while `ppx` handles package discovery/materialization.

```sh
ppx search requests
ppx add requests
ppx tree
```

Bundled first-party packages work offline. The included local PPX registry backend supports authenticated immutable publication, checksums, safe archive validation, search, downloads and yanking for development/testing.
