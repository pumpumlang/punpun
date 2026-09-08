#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import shutil
import tomllib

ROOT=Path(__file__).resolve().parent
OUT=ROOT/'dist'
PROJECT_ROOT=ROOT.parent
VERSION=(PROJECT_ROOT/'VERSION').read_text(encoding='utf-8').strip()


def package_digest(package_root: Path) -> str:
    digest=hashlib.sha256()
    for path in sorted(package_root.rglob('*')):
        if not path.is_file():
            continue
        digest.update(path.relative_to(package_root).as_posix().encode())
        digest.update(b'\0')
        digest.update(path.read_bytes())
        digest.update(b'\0')
    return digest.hexdigest()


def bundled_catalog() -> dict:
    packages=[]
    for manifest in sorted((PROJECT_ROOT/'packages').glob('*/Punpun.toml')):
        metadata=tomllib.loads(manifest.read_text(encoding='utf-8'))['package']
        package_root=manifest.parent
        version=str(metadata['version'])
        packages.append({
            'name':str(metadata['name']),
            'description':str(metadata.get('description','PunPun package')),
            'latest_version':version,
            'version':version,
            'owner':'PunPun Project',
            'bundled':True,
            'checksum':package_digest(package_root),
            'versions':[{'version':version,'yanked':False,'bundled':True}],
        })
    return {'schema':1,'release':VERSION,'packages':packages}


def main() -> None:
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir()
    for name in ('index.html','package.html','security.html'):
        rendered=(ROOT/'src'/name).read_text(encoding='utf-8').replace('{{VERSION}}',VERSION)
        (OUT/name).write_text(rendered,encoding='utf-8')
    shutil.copytree(ROOT/'static', OUT/'static')
    shutil.copy2(ROOT/'README.md',OUT/'README.md')
    (OUT/'static'/'catalog.json').write_text(
        json.dumps(bundled_catalog(),indent=2,sort_keys=True)+'\n',encoding='utf-8'
    )
    print(f'built PPX site -> {OUT}')


if __name__=='__main__':
    main()
