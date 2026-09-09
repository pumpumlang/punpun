#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; BASELINE=ROOT/'spec/1.0/stable-api.json'
def info(ppc): return json.loads(subprocess.run([str(ppc),'language-info'],cwd=ROOT,check=True,text=True,stdout=subprocess.PIPE).stdout)
def norm(i): return {'schema':1,'language_version':i['language_version'],'abi_version':i['abi_version'],'runtime_abi_version':i['runtime_abi_version'],'lockfile_format':i['lockfile_format'],'package_format':i['package_format'],'keywords':sorted(i['keywords']),'builtins':sorted(i['builtins'],key=lambda x:x['name']),'stdlib_symbols':sorted(i['stdlib_symbols'],key=lambda x:(x['module'],x['name'],json.dumps(x,sort_keys=True)))}
def ident_std(x): return x['module']+'::'+x['name']+'('+','.join(p['type'] for p in x['parameters'])+')->'+x['result']
def check(cur,base):
 e=[]
 for f in ('language_version','abi_version','runtime_abi_version','lockfile_format','package_format'):
  if cur.get(f)!=base.get(f): e.append(f'{f} changed: {base.get(f)!r} -> {cur.get(f)!r}')
 miss=sorted(set(base['keywords'])-set(cur['keywords']))
 if miss: e.append('stable keywords removed: '+', '.join(miss))
 cb={x['name']:x for x in cur['builtins']}
 for x in base['builtins']:
  if x['name'] not in cb: e.append('stable builtin removed: '+x['name'])
  elif cb[x['name']]!=x: e.append('stable builtin signature changed: '+x['name'])
 cs={ident_std(x) for x in cur['stdlib_symbols']}
 for x in base['stdlib_symbols']:
  if ident_std(x) not in cs: e.append('stable stdlib symbol removed/changed: '+ident_std(x))
 return e
def main():
 ap=argparse.ArgumentParser(); sub=ap.add_subparsers(dest='cmd',required=True)
 for n in ('snapshot','check'):
  p=sub.add_parser(n); p.add_argument('--ppc',default=str(ROOT/'build/ppc'))
 a=ap.parse_args(); cur=norm(info(Path(a.ppc)))
 if a.cmd=='snapshot': BASELINE.parent.mkdir(parents=True,exist_ok=True); BASELINE.write_text(json.dumps(cur,indent=2,sort_keys=True)+'\n'); print(BASELINE); return
 if not BASELINE.is_file(): raise SystemExit('stability: missing spec/1.0/stable-api.json')
 base=json.loads(BASELINE.read_text()); errors=check(cur,base)
 if errors:
  [print('BREAKING: '+x) for x in errors]; raise SystemExit(f'stability: {len(errors)} breaking change(s) against PunPun 1.0')
 print(f"PunPun 1.0 stable surface: PASS ({len(base['builtins'])} builtins, {len(base['stdlib_symbols'])} stdlib signatures)")
if __name__=='__main__': main()
