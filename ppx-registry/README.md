# PPX Registry Backend

A dependency-free local development registry for PunPunXPac. It stores package
metadata in SQLite and immutable package ZIPs on disk. Uploaded archives are
size-limited, path-validated and checksum-verified; package source is never
executed by the registry.

```sh
python3 server.py --data .ppx-registry
curl -X POST http://127.0.0.1:8765/api/v1/register \
  -H 'content-type: application/json' \
  -d '{"username":"developer","password":"change-me-now"}'
ppx login developer change-me-now
ppx publish
```

This beta server is intended for local/integration testing. Internet deployment
still needs a reverse proxy, TLS termination, rate limiting, backups and a
production identity/email recovery system.
