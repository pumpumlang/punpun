# PunPun 0.9 PPX integrity contract

New PPX publish archives contain `PPX-MANIFEST.json`. The manifest format version is `1` and records every packaged source file using its archive-relative path, byte size and SHA-256 digest.

Verification rejects missing/mismatched manifest entries, digest mismatches and unsafe archive layout. `ppx verify` requires the manifest. Install/download verifies it when present so older registry material can still be consumed during migration.

Remote non-loopback registries require HTTPS by default. Plain HTTP requires the explicit `PPX_ALLOW_INSECURE_REGISTRY=1` development override. Loopback HTTP remains allowed for local tests/development.

The 0.9 manifest is an integrity mechanism, not publisher-identity authentication. It does not constitute public-key package signing.
