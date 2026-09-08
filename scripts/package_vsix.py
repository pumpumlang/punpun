#!/usr/bin/env python3
from pathlib import Path
import json, shutil, zipfile
ROOT=Path(__file__).resolve().parents[1]
EXT=ROOT/'editors'/'vscode'
OUT=ROOT/'dist'/'punpun-vscode-0.5.0-beta.vsix'
# Keep the extension's bundled server synchronized with the real LSP implementation.
(EXT/'server').mkdir(exist_ok=True)
shutil.copy2(ROOT/'tooling'/'lsp'/'server.js', EXT/'server'/'server.js')
pkg=json.loads((EXT/'package.json').read_text())
version=pkg['version']
manifest=f'''<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="punpun" Version="{version}" Publisher="punpun" />
    <DisplayName>PunPun</DisplayName>
    <Description xml:space="preserve">PunPun {version} language support powered by the compiler semantic engine.</Description>
    <Categories>Programming Languages</Categories>
    <Properties><Property Id="Microsoft.VisualStudio.Code.Engine" Value="{pkg['engines']['vscode']}" /></Properties>
  </Metadata>
  <Installation><InstallationTarget Id="Microsoft.VisualStudio.Code" /></Installation>
  <Dependencies />
  <Assets><Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" /></Assets>
</PackageManifest>'''
content_types='''<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="json" ContentType="application/json" />
  <Default Extension="js" ContentType="application/javascript" />
  <Default Extension="md" ContentType="text/markdown" />
  <Default Extension="png" ContentType="image/png" />
  <Default Extension="svg" ContentType="image/svg+xml" />
  <Default Extension="vsixmanifest" ContentType="text/xml" />
</Types>'''
OUT.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(OUT,'w',zipfile.ZIP_DEFLATED) as z:
    z.writestr('extension.vsixmanifest',manifest)
    z.writestr('[Content_Types].xml',content_types)
    for p in sorted(EXT.rglob('*')):
        if p.is_file(): z.write(p,'extension/'+p.relative_to(EXT).as_posix())
# Validate critical files that were missing from an earlier beta VSIX.
with zipfile.ZipFile(OUT) as z:
    names=set(z.namelist())
    required={'extension/package.json','extension/extension.js','extension/server/server.js','extension/assets/punpun-icon-128.png','extension/syntaxes/punpun.tmLanguage.json'}
    missing=required-names
    if missing: raise SystemExit('VSIX missing: '+', '.join(sorted(missing)))
print(OUT)
