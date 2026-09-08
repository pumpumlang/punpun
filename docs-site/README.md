# PunPun Documentation Website

Dependency-free static documentation site. Build with:

```sh
python3 build.py
python3 -m http.server 8000 --directory dist
```

Content lives in `content/*.md`. The build generates responsive HTML, a search
index, dark/light themes and copyable code blocks. It deliberately documents
implemented beta behavior and labels incomplete areas instead of advertising
roadmap items as released features.
