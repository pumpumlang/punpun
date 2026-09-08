#!/usr/bin/env python3
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parent
OUT=ROOT/'dist'
if OUT.exists(): shutil.rmtree(OUT)
OUT.mkdir()
for name in ('index.html','package.html','security.html'):
    shutil.copy2(ROOT/'src'/name, OUT/name)
shutil.copytree(ROOT/'static', OUT/'static')
print(f'built PPX site -> {OUT}')
