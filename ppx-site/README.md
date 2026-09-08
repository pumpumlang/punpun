# PPX registry site

Static frontend for the PunPunXPac beta registry. It uses no build-time dependencies.

```sh
python3 build.py
python3 ../ppx-registry/server.py
python3 -m http.server 8080 -d dist
```

Set browser `localStorage.ppxRegistry` to point at a different registry API. The bundled registry is a development reference implementation, not a hosted production service.
