<p align="center"><img src="../assets/punpun-mark.svg" width="88" alt="PunPun"></p>

# PunPun Documentation Site

<p align="center"><strong>Fast, searchable and readable without a JavaScript build toolchain.</strong></p>

The documentation website is a dependency-free static project with responsive navigation, dark/light themes, client-side search and copyable code examples.

## Preview locally

```sh
python3 docs-site/build.py
python3 -m http.server 8000 --directory docs-site/dist
```

Open <http://localhost:8000> and stop the server with `Ctrl+C`.

## Edit documentation

1. Change or add Markdown in `docs-site/content/`.
2. Run `python3 docs-site/build.py`.
3. Open the generated pages in `docs-site/dist/`.
4. Run `./tests/run.sh` before publishing.

The builder emits plain HTML, CSS, JavaScript and `search.json`. It documents implemented beta behavior and labels unfinished production areas instead of advertising roadmap work as shipped.

## Publish

The release bundle contains `PunPun-0.5.0-beta-docs-site.zip`. Run the bundled `publish-punpun.sh` to update the `punpun-docs` GitHub Pages repository and print its exact URL.
