#!/usr/bin/env python3
from __future__ import annotations
import argparse,hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser(); ap.add_argument('--root',default=str(ROOT)); ap.add_argument('-o','--output',required=True); a=ap.parse_args(); root=Path(a.root).resolve(); files=[]; ignored={'.git','build','dist','.punpun','__pycache__','node_modules'}
 for p in sorted(root.rglob('*')):
  if not p.is_file() or any(x in ignored for x in p.relative_to(root).parts): continue
  files.append({'path':p.relative_to(root).as_posix(),'sha256':sha(p),'size':p.stat().st_size})
 doc={'spdxVersion':'SPDX-2.3','SPDXID':'SPDXRef-DOCUMENT','name':'PunPun-source','dataLicense':'CC0-1.0','documentNamespace':'https://github.com/pumpumlang/punpun/releases/punpun-source-sbom','files':files}; Path(a.output).write_text(json.dumps(doc,indent=2,sort_keys=True)+'\n'); print(f'SBOM: {len(files)} source files -> {a.output}')
if __name__=='__main__': main()
