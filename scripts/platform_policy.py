#!/usr/bin/env python3
from __future__ import annotations
import argparse,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; POLICY=ROOT/"support/platforms.json"
def load():
 d=json.loads(POLICY.read_text());
 if d.get("schema")!=1 or d.get("release_line")!="1.x": raise SystemExit("platform policy: unsupported schema/release line")
 t={x.get("target"):x for x in d.get("tiers",[])}
 if not {"linux-x86_64","arch-x86_64","windows-x86_64"} <= set(t): raise SystemExit("platform policy: required target missing")
 if t["linux-x86_64"].get("tier")!=1: raise SystemExit("platform policy: Linux x86-64 must be Tier 1 for 1.0")
 return d
def main():
 ap=argparse.ArgumentParser(); ap.add_argument("--check",action="store_true"); a=ap.parse_args(); d=load()
 if a.check: print("platform support policy: PASS"); return
 print("PunPun 1.x platform support tiers")
 for x in d["tiers"]: print(f"Tier {x['tier']}  {x['target']:<18} {x['status']}\n  {x['promise']}")
if __name__=="__main__": main()
