# PPX package security

PPX package bytes are immutable and checksum-verified. Step 9 adds an internal
`PPX-MANIFEST.json` to every newly published archive. The manifest records the
path, size, and SHA-256 of every package file and is verified after extraction.

Useful commands:

```sh
ppx verify package.zip
ppx audit
ppx audit --deny-injection
```

`ppx audit` inspects materialized local dependency paths and reports native
`@inject` surfaces. It does not pretend native injection can be made harmless by
renaming it.

Remote registries must use HTTPS. Plain HTTP is accepted automatically only for
loopback development servers. A non-loopback HTTP registry requires an explicit
`PPX_ALLOW_INSECURE_REGISTRY=1` opt-in.
