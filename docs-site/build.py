#!/usr/bin/env python3
from __future__ import annotations
from html import escape
import json
from pathlib import Path
import re, shutil

ROOT=Path(__file__).resolve().parent
CONTENT=ROOT/'content'; DIST=ROOT/'dist'; STATIC=ROOT/'static'
VERSION=(ROOT.parent/'VERSION').read_text(encoding='utf-8').strip()

def inline(text:str)->str:
    text=escape(text)
    text=re.sub(r'`([^`]+)`',r'<code>\1</code>',text)
    text=re.sub(r'\[([^]]+)\]\(([^)]+)\)',r'<a href="\2">\1</a>',text)
    return text

def markdown(src:str)->tuple[str,str]:
    out=[]; title='PunPun'; lines=src.splitlines(); i=0; in_code=False; code=[]; lang=''; list_open=False
    while i<len(lines):
        line=lines[i]
        if line.startswith('```'):
            if not in_code:
                if list_open: out.append('</ul>'); list_open=False
                in_code=True; lang=line[3:].strip(); code=[]
            else:
                body=escape('\n'.join(code)); cls=f' class="language-{escape(lang)}"' if lang else ''
                out.append(f'<div class="code-wrap"><button class="copy" aria-label="Copy code">Copy</button><pre><code{cls}>{body}</code></pre></div>')
                in_code=False
            i+=1; continue
        if in_code: code.append(line); i+=1; continue
        if line.startswith('# '):
            if title=='PunPun': title=line[2:].strip()
            out.append(f'<h1>{inline(line[2:].strip())}</h1>')
        elif line.startswith('## '): out.append(f'<h2>{inline(line[3:].strip())}</h2>')
        elif line.startswith('### '): out.append(f'<h3>{inline(line[4:].strip())}</h3>')
        elif line.startswith('- '):
            if not list_open: out.append('<ul>'); list_open=True
            out.append(f'<li>{inline(line[2:].strip())}</li>')
        elif not line.strip():
            if list_open: out.append('</ul>'); list_open=False
        else:
            if list_open: out.append('</ul>'); list_open=False
            out.append(f'<p>{inline(line.strip())}</p>')
        i+=1
    if list_open: out.append('</ul>')
    return '\n'.join(out),title

def template(title,body,nav):
    return f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="description" content="PunPun {VERSION} language documentation"><title>{escape(title)} · PunPun</title><link rel="stylesheet" href="assets/site.css"></head><body><header><a class="brand" href="index.html"><img src="assets/punpun-mark.svg" alt="PunPun">PunPun <span>{VERSION}</span></a><div class="head-actions"><input id="search" placeholder="Search docs…" aria-label="Search documentation"><button id="theme" aria-label="Toggle theme">◐</button></div></header><div class="layout"><aside><nav>{nav}</nav><div id="results"></div></aside><main>{body}<footer>Development documentation. Features marked planned are not yet implemented.</footer></main></div><script src="assets/site.js"></script></body></html>'''

def main():
    shutil.rmtree(DIST,ignore_errors=True); (DIST/'assets').mkdir(parents=True)
    shutil.copy(STATIC/'site.css',DIST/'assets/site.css'); shutil.copy(STATIC/'site.js',DIST/'assets/site.js')
    shutil.copy(ROOT.parent/'assets/punpun-mark.svg',DIST/'assets/punpun-mark.svg')
    shutil.copy(ROOT/'README.md',DIST/'README.md')
    pages=[]
    for path in sorted(CONTENT.glob('*.md')):
        body,title=markdown(path.read_text(encoding='utf-8')); slug=path.stem
        pages.append({'slug':slug,'title':title,'body':re.sub('<[^>]+>',' ',body)[:12000],'html':body})
    nav=''.join(f'<a href="{p["slug"]}.html">{escape(p["title"])}</a>' for p in pages)
    for p in pages:
        (DIST/f'{p["slug"]}.html').write_text(template(p['title'],p['html'],nav),encoding='utf-8')
    home=next((p for p in pages if p['slug']=='index'),pages[0]); (DIST/'index.html').write_text(template(home['title'],home['html'],nav),encoding='utf-8')
    (DIST/'search.json').write_text(json.dumps([{k:p[k] for k in ('slug','title','body')} for p in pages]),encoding='utf-8')
    print(f'built {len(pages)} documentation pages -> {DIST}')
if __name__=='__main__': main()
