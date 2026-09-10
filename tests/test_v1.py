#!/usr/bin/env python3
from __future__ import annotations
import json,shutil,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]; PPC=ROOT/'build/ppc'; PP=ROOT/'pp'; PPX=ROOT/'ppx/ppx.py'
def run(args,cwd=ROOT,timeout=90):
 r=subprocess.run([str(x) for x in args],cwd=cwd,text=True,capture_output=True,timeout=timeout)
 if r.returncode: raise AssertionError(f"failed: {args}\\nstdout={r.stdout}\\nstderr={r.stderr}")
 return r
class StableMetadata(unittest.TestCase):
 def test_language_info_is_stable(self):
  i=json.loads(run([PPC,'language-info']).stdout); self.assertEqual(i['compiler_version'],'1.3.0'); self.assertEqual(i['language_version'],'1.0'); self.assertEqual(i['language_stability'],'stable'); self.assertEqual(i['abi_version'],1); self.assertEqual(i['runtime_abi_version'],1); self.assertEqual(i['lockfile_format'],1); self.assertEqual(i['package_format'],1)
 def test_stability_and_runtime_abi(self): run([sys.executable,ROOT/'scripts/stability.py','check','--ppc',PPC]); run([sys.executable,ROOT/'scripts/abi_check.py','--ppc',PPC])
 def test_new_project_freezes_language_and_abi(self):
  with tempfile.TemporaryDirectory() as td:
   run([PP,'new','stable_demo'],cwd=td); m=(Path(td)/'stable_demo/Punpun.toml').read_text(); self.assertIn('language = "1.0"',m); self.assertIn('abi = 1',m); run([PP,'update'],cwd=Path(td)/'stable_demo'); lock=(Path(td)/'stable_demo/Punpun.lock').read_text(); self.assertIn('language = "1.0"',lock); self.assertIn('abi = 1',lock)
 def test_incompatible_manifest_rejected(self):
  with tempfile.TemporaryDirectory() as td:
   q=Path(td); (q/'src').mkdir(); (q/'src/main.pp').write_text('launch { say("x"); }\n'); (q/'Punpun.toml').write_text('[package]\nname="bad"\nversion="1.0.0"\nlanguage="2.0"\nabi=2\nentry="src/main.pp"\n'); r=subprocess.run([PPC,'check',q/'src/main.pp'],cwd=q,text=True,capture_output=True); self.assertNotEqual(r.returncode,0); self.assertIn('unsupported PunPun language',r.stderr)
class Signing(unittest.TestCase):
 @unittest.skipUnless(shutil.which('openssl'),'OpenSSL required')
 def test_release_signature_tamper_detection(self):
  with tempfile.TemporaryDirectory() as td:
   q=Path(td); d=q/'r'; d.mkdir(); (d/'a.txt').write_text('stable\n'); priv=q/'priv.pem'; pub=q/'pub.pem'; run([sys.executable,ROOT/'scripts/release_sign.py','keygen','--private-key',priv,'--public-key',pub]); run([sys.executable,ROOT/'scripts/release_sign.py','sign-release',d,'--private-key',priv,'--public-key',pub]); run([sys.executable,ROOT/'scripts/release_sign.py','verify-release',d,'--public-key',pub]); (d/'a.txt').write_text('tampered\n'); r=subprocess.run([sys.executable,ROOT/'scripts/release_sign.py','verify-release',d,'--public-key',pub],text=True,capture_output=True); self.assertNotEqual(r.returncode,0)
class PpxSigning(unittest.TestCase):
 @unittest.skipUnless(shutil.which('openssl'),'OpenSSL required')
 def test_ppx_detached_signature(self):
  with tempfile.TemporaryDirectory() as td:
   q=Path(td); pkg=q/'pkg'; (pkg/'src').mkdir(parents=True); (pkg/'Punpun.toml').write_text('[package]\nname="signed"\nversion="1.0.0"\nentry="src/main.pp"\n\n[dependencies]\n'); (pkg/'src/main.pp').write_text('fn answer() -> i64 { return 42; }\n')
   helper='import importlib.util,pathlib; p=pathlib.Path(r"'+str(PPX)+'"); s=importlib.util.spec_from_file_location("px",p); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); c,_=m.create_package_archive(pathlib.Path(r"'+str(pkg)+'")); pathlib.Path(r"'+str(q/'pkg.zip')+'").write_bytes(c)'
   run([sys.executable,'-c',helper]); priv=q/'priv.pem'; pub=q/'pub.pem'; sig=q/'pkg.zip.sig'; run([sys.executable,ROOT/'scripts/release_sign.py','keygen','--private-key',priv,'--public-key',pub]); run([sys.executable,PPX,'sign',q/'pkg.zip','--private-key',priv,'-o',sig]); run([sys.executable,PPX,'verify',q/'pkg.zip','--signature',sig,'--public-key',pub])

class Platform(unittest.TestCase):
 def test_policy(self): run([sys.executable,ROOT/'scripts/platform_policy.py','--check'])
if __name__=='__main__': unittest.main(verbosity=2)
