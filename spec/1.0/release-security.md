# PunPun 1.0 release security and reproducibility

PunPun 1.0 release assembly uses fixed timestamps, SHA-256 checksums, a source SBOM, release provenance, and optional detached **Ed25519** signatures through OpenSSL. Private keys are never stored in source or publisher bundles. PPX archives retain internal hashes and may additionally use detached Ed25519 publisher signatures with local trust roots.
