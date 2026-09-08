<p align="center"><img src="../ppx-site/static/ppx-mark.svg" width="80" alt="PPX"></p>

# PPX Development Registry

This is the reference HTTP registry used for PPX development and acceptance tests. It provides real register, login, publish, search, metadata, download, yank and logout flows with checksum validation.

> This server is intentionally labeled **development**. Run it locally or behind your own controlled infrastructure; do not mistake it for the not-yet-deployed public PPX production service.

## Run locally

```sh
python3 server.py --host 127.0.0.1 --port 8765 --data .ppx-registry
```

In a second terminal:

```sh
curl -X POST http://127.0.0.1:8765/api/v1/register \
  -H 'Content-Type: application/json' \
  -d '{"username":"developer","password":"change-me-now"}'

ppx login developer change-me-now
ppx publish
ppx search your-package
```

## Implemented safeguards

- password and token hashing;
- token revocation;
- immutable package versions;
- SHA-256 archive verification;
- package-name validation;
- archive path-traversal rejection;
- upload and expanded-size limits;
- metadata-only search that never executes package source.

## Production checklist

A public deployment still needs hardened identity, TLS termination, a durable database/object store, backups and recovery, rate limits, monitoring, moderation, organization ownership, provenance signing and a complete semantic-version conflict solver.

The public static website does not depend on this process: it uses a bundled first-party catalog until a production endpoint is intentionally configured.
