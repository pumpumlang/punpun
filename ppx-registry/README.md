<p align="center"><img src="../ppx-site/static/ppx-mark.svg" width="80" alt="PPX"></p>

# PPX Development Registry

This is the reference HTTP registry used for PPX development and acceptance tests. It supports account registration, login/logout, authenticated package publication, search, metadata, checksum-verified download, immutable versions, yanking, and basic request rate limiting.

> This server is a **development/reference service**, not a claim that a production public PPX service is already deployed. Uploaded package source is stored and served; it is never executed by the registry.

## Run locally

```sh
python3 server.py --host 127.0.0.1 --port 8765 --data .ppx-registry
```

In another terminal:

```sh
export PPX_REGISTRY=http://127.0.0.1:8765
ppx register developer
ppx login developer
cd your-package
ppx publish --dry-run
ppx publish
ppx info your-package
```

Consume the package from another PunPun project:

```sh
ppx install your-package '^1.0.0'
```

Or download the immutable archive without editing a project:

```sh
ppx download your-package 1.0.0 -o your-package-1.0.0.zip
```

## Validation performed by the registry

- package identity/version are read from the uploaded archive's root `Punpun.toml`;
- payload identity/dependency metadata cannot contradict the archive manifest;
- package names and semantic versions are validated;
- registry dependency requirements are validated and local path dependencies are rejected;
- descriptions, URLs, readme paths, license text, and keywords have bounded/validated forms;
- ZIP traversal, duplicate paths, symlinks, excessive file counts, archive size, and expanded size are rejected;
- the registry computes SHA-256 itself and checks any client-supplied digest;
- published versions are immutable and ownership is enforced on later publishes/yanks;
- authentication tokens are hashed at rest and expire;
- publication never runs uploaded source code.

## Production checklist

A public deployment still needs production-grade identity/recovery, TLS termination, durable database/object storage, backups and disaster recovery, distributed/edge rate limiting, audit logging, monitoring/alerting, abuse moderation, organization ownership, provenance/signing, retention policy, and operational incident procedures.
