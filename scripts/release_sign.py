#!/usr/bin/env python3
from __future__ import annotations
import argparse,base64,hashlib,json,os,shutil,subprocess,tempfile
from pathlib import Path
def need():
 if not shutil.which('openssl'): raise SystemExit('release signing requires OpenSSL 3.x with Ed25519 support')
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def run(a,**kw): return subprocess.run([str(x) for x in a],check=True,**kw)
def keygen(priv,pub): need(); priv.parent.mkdir(parents=True,exist_ok=True); pub.parent.mkdir(parents=True,exist_ok=True); run(['openssl','genpkey','-algorithm','ED25519','-out',priv]); os.chmod(priv,0o600); run(['openssl','pkey','-in',priv,'-pubout','-out',pub]); print(f'generated Ed25519 signing key: {priv}\npublic key: {pub}')
def sign_bytes(priv,data):
 need()
 with tempfile.TemporaryDirectory() as td:
  td=Path(td); m=td/'m'; s=td/'s'; m.write_bytes(data); run(['openssl','pkeyutl','-sign','-rawin','-inkey',priv,'-in',m,'-out',s]); return s.read_bytes()
def verify_bytes(pub,data,sig):
 need()
 with tempfile.TemporaryDirectory() as td:
  td=Path(td); m=td/'m'; s=td/'s'; m.write_bytes(data); s.write_bytes(sig); return subprocess.run(['openssl','pkeyutl','-verify','-rawin','-pubin','-inkey',pub,'-in',m,'-sigfile',s],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0
def payload(d): return {'schema':1,'algorithm':'Ed25519','artifacts':[{'filename':p.name,'sha256':sha(p),'size':p.stat().st_size} for p in sorted(d.iterdir()) if p.is_file() and p.name not in {'RELEASE_SIGNATURES.json','RELEASE_PROVENANCE.json'} and not p.name.endswith('.sig')]}
def sign_release(d,priv,pub=None):
 p=payload(d); canonical=(json.dumps(p,sort_keys=True,separators=(',',':'))+'\n').encode(); doc={**p,'signature':base64.b64encode(sign_bytes(priv,canonical)).decode()};
 if pub: doc['public_key_sha256']=sha(pub)
 (d/'RELEASE_SIGNATURES.json').write_text(json.dumps(doc,indent=2,sort_keys=True)+'\n'); print('signed release manifest with Ed25519')
def verify_release(d,pub=None):
 sf=d/'RELEASE_SIGNATURES.json'
 if not sf.is_file():
  sums=d/'SHA256SUMS'
  if not sums.is_file(): raise SystemExit('release verify: no signature or SHA256SUMS')
  for line in sums.read_text().splitlines():
   digest,name=line.split(None,1); q=d/name.strip()
   if not q.is_file() or sha(q)!=digest: raise SystemExit('release verify: checksum mismatch: '+name.strip())
  print('release integrity: PASS (SHA256SUMS; unsigned local build)'); return
 doc=json.loads(sf.read_text()); sig=base64.b64decode(doc.pop('signature')); ph=doc.pop('public_key_sha256',None); expected=payload(d)
 if any(doc.get(k)!=expected.get(k) for k in ('schema','algorithm','artifacts')): raise SystemExit('release verify: signed manifest does not match directory')
 if pub is None: raise SystemExit('release verify: signature present; provide --public-key')
 if ph and sha(pub)!=ph: raise SystemExit('release verify: public key fingerprint mismatch')
 canonical=(json.dumps(expected,sort_keys=True,separators=(',',':'))+'\n').encode()
 if not verify_bytes(pub,canonical,sig): raise SystemExit('release verify: Ed25519 signature invalid')
 print('release signature: PASS (Ed25519)')
def main():
 ap=argparse.ArgumentParser(); sub=ap.add_subparsers(dest='cmd',required=True)
 k=sub.add_parser('keygen'); k.add_argument('--private-key',required=True); k.add_argument('--public-key',required=True)
 s=sub.add_parser('sign-release'); s.add_argument('directory'); s.add_argument('--private-key',required=True); s.add_argument('--public-key')
 v=sub.add_parser('verify-release'); v.add_argument('directory'); v.add_argument('--public-key'); a=ap.parse_args()
 if a.cmd=='keygen': keygen(Path(a.private_key),Path(a.public_key))
 elif a.cmd=='sign-release': sign_release(Path(a.directory),Path(a.private_key),Path(a.public_key) if a.public_key else None)
 else: verify_release(Path(a.directory),Path(a.public_key) if a.public_key else None)
if __name__=='__main__': main()
