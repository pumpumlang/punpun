<p align="center"><img src="static/ppx-mark.svg" width="92" alt="PPX"></p>

# PunPunXPac Package Catalog

<p align="center"><strong>The fast, dependency-free frontend for PunPun packages.</strong></p>

This repository powers the public PPX catalog. It ships a generated snapshot of every first-party package, so search and package pages work immediately on GitHub Pages—no localhost server and no production API required.

## Build and preview

From the PunPun source tree:

```sh
python3 ppx-site/build.py
python3 -m http.server 8080 --directory ppx-site/dist
```

Open <http://localhost:8080>.

## How data works

`build.py` reads each `packages/*/Punpun.toml` and generates `dist/static/catalog.json`. The frontend searches that catalog by default.

To test a live PPX API, set an endpoint in the browser console and reload:

```js
localStorage.setItem('ppxRegistry', 'https://your-registry.example')
```

Remove the override to return to the bundled catalog:

```js
localStorage.removeItem('ppxRegistry')
```

If an optional live endpoint is unreachable, the site quietly falls back to the bundled catalog rather than showing a broken “local registry not running” page.

## Deployment

The release bundle contains `PunPun-0.5.0-beta-ppx-site.zip`, already built for static hosting. The top-level `publish-punpun.sh` script creates or updates the `punpun-ppx` repository, enables GitHub Pages and prints the correct public URL.

No access token or registry credential belongs in this static site.
