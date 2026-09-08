# PunPunXPac (PPX)

PPX is PunPun's beta package client. It uses the same `Punpun.toml` and
`Punpun.lock` consumed by `pp`; it does not create a second compiler/package
model. Bundled and local packages work offline. Registry operations use the
local PPX HTTP API described in `../ppx-registry/`.

```sh
ppx search requests
ppx add requests
ppx tree
ppx doctor
```

Set `PPX_REGISTRY` to choose a registry endpoint. The default is the local
development server at `http://127.0.0.1:8765`.
