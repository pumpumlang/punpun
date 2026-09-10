#!/usr/bin/env python3
from __future__ import annotations
import argparse,json,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 ap=argparse.ArgumentParser(); ap.add_argument('--ppc',default=str(ROOT/'build/ppc')); a=ap.parse_args()
 i=json.loads(subprocess.run([a.ppc,'language-info'],cwd=ROOT,check=True,text=True,stdout=subprocess.PIPE).stdout)
 if i.get('abi_version')!=1 or i.get('runtime_abi_version')!=1: raise SystemExit('ABI check: compiler metadata is not ABI 1')
 src='#include "ppcrt.h"\n#if PUNPUN_RUNTIME_ABI_VERSION != 1\n#error bad runtime ABI\n#endif\nint main(void){pp_runtime_init(0,0);return pp_runtime_abi_version()==1?0:1;}\n'
 with tempfile.TemporaryDirectory() as td:
  td=Path(td); c=td/'abi.c'; exe=td/'abi'; c.write_text(src)
  runtime=[ROOT/'runtime/ppcrt.c',ROOT/'runtime/ppc_https.c',ROOT/'runtime/ppc_gui.c',ROOT/'runtime/ppc_platform_posix.c',ROOT/'runtime/ppc_platform_windows.c']
  subprocess.run(['cc','-std=c11','-I',str(ROOT/'runtime'),str(c),*[str(x) for x in runtime],'-pthread','-lm','-ldl','-o',str(exe)],check=True,cwd=ROOT); subprocess.run([str(exe)],check=True)
 print('PunPun compiler/runtime ABI 1: PASS')
if __name__=='__main__': main()
