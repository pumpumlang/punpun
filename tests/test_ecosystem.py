import base64
import hashlib
import importlib.util
import io
import os
import http.server
import threading
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PP = ROOT / 'pp'
PPX = ROOT / 'ppx' / 'ppx.py'


def run_checked(args, **kwargs):
    """Run a test subprocess while preserving useful diagnostics in CI."""
    result = subprocess.run(args, check=False, text=True, capture_output=True, **kwargs)
    if result.returncode != 0:
        command = ' '.join(map(str, args))
        raise AssertionError(
            f"command failed ({result.returncode}): {command}\n"
            f"--- stdout ---\n{result.stdout}\n--- stderr ---\n{result.stderr}"
        )
    return result


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader
    spec.loader.exec_module(mod)
    return mod


class PpxUnitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ppx = load_module(PPX, 'punpun_ppx_test')
        cls.registry_mod = load_module(ROOT/'ppx-registry'/'server.py', 'punpun_registry_test')

    def test_semver_ranges(self):
        s = self.ppx.satisfies
        self.assertTrue(s('1.5.2', '^1.2.0'))
        self.assertFalse(s('2.0.0', '^1.2.0'))
        self.assertTrue(s('0.2.5', '^0.2.0'))
        self.assertTrue(s('1.4.9', '~1.4.0'))
        self.assertFalse(s('1.5.0', '~1.4.0'))
        self.assertTrue(s('2.1.0', '>=2.0.0,<3.0.0'))
        self.assertFalse(s('2.1.0-beta.1', '>=2.0.0,<3.0.0'))
        self.assertTrue(s('2.1.0-beta.1', '2.1.0-beta.1'))

    def test_registry_token_revocation_and_semver_search_order(self):
        with tempfile.TemporaryDirectory() as td:
            registry=self.registry_mod.Registry(Path(td))
            registry.register('tester','abcdefgh')
            token=registry.login('tester','abcdefgh')
            user=registry.authenticate(token)
            self.assertIsNotNone(user)
            for version in ('2.0.0','10.0.0'):
                buf=io.BytesIO()
                with zipfile.ZipFile(buf,'w',zipfile.ZIP_DEFLATED) as zf:
                    zf.writestr('Punpun.toml',f'[package]\nname="demo"\nversion="{version}"\nentry="src/main.pp"\n')
                    zf.writestr('src/main.pp','launch {}\n')
                raw=buf.getvalue()
                registry.publish(user,{'name':'demo','version':version,'description':'demo','dependencies':{},
                                        'checksum':hashlib.sha256(raw).hexdigest(),'archive_b64':base64.b64encode(raw).decode()})
            self.assertEqual(registry.search('demo')[0]['latest'],'10.0.0')
            registry.logout(token)
            self.assertIsNone(registry.authenticate(token))

    def test_registry_publish_is_immutable_and_checksummed(self):
        with tempfile.TemporaryDirectory() as td:
            registry = self.registry_mod.Registry(Path(td))
            registry.register('tester', 'abcdefgh')
            token = registry.login('tester', 'abcdefgh')
            user = registry.authenticate(token)
            buf = io.BytesIO()
            with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as zf:
                zf.writestr('Punpun.toml', '[package]\nname = "demo"\nversion = "1.0.0"\nentry = "src/main.pp"\n')
                zf.writestr('src/main.pp', 'launch { say("hi"); }\n')
            raw = buf.getvalue(); checksum = hashlib.sha256(raw).hexdigest()
            payload = {'name':'demo','version':'1.0.0','description':'demo','dependencies':{},
                       'checksum':checksum,'archive_b64':base64.b64encode(raw).decode()}
            self.assertEqual(registry.publish(user, payload), checksum)
            with self.assertRaises(FileExistsError): registry.publish(user, payload)
            meta = registry.package_metadata('demo')
            self.assertEqual(meta['versions'][0]['checksum'], checksum)
            self.assertEqual(hashlib.sha256(registry.download('demo','1.0.0')).hexdigest(), checksum)

    def test_registry_rejects_path_traversal(self):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, 'w') as zf:
            zf.writestr('Punpun.toml','x')
            zf.writestr('../escape','bad')
        with self.assertRaises(ValueError): self.registry_mod.Registry.validate_archive(buf.getvalue())

    def test_publish_manifest_metadata_and_path_dependency_policy(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            (root/'README.md').write_text('# Demo\n')
            (root/'Punpun.toml').write_text(
                '[package]\nname="demo_pkg"\nversion="1.2.3"\ndescription="demo"\n'
                'license="MIT"\nrepository="https://github.com/example/demo"\nreadme="README.md"\n'
                'keywords=["demo","math"]\n\n[dependencies]\njson="^0.1.0"\n'
            )
            metadata=self.ppx.manifest_publish_metadata(root)
            self.assertEqual(metadata['name'],'demo_pkg')
            self.assertEqual(metadata['dependencies'],{'json':'^0.1.0'})
            self.assertEqual(metadata['license'],'MIT')
            self.assertEqual(metadata['keywords'],['demo','math'])
            (root/'Punpun.toml').write_text(
                '[package]\nname="demo_pkg"\nversion="1.2.3"\n\n[dependencies]\nlocal={ path="../local" }\n'
            )
            with self.assertRaises(SystemExit): self.ppx.manifest_publish_metadata(root)

    def test_registry_uses_manifest_identity_not_spoofed_payload(self):
        with tempfile.TemporaryDirectory() as td:
            registry=self.registry_mod.Registry(Path(td))
            registry.register('tester','abcdefgh')
            user=registry.authenticate(registry.login('tester','abcdefgh'))
            buf=io.BytesIO()
            with zipfile.ZipFile(buf,'w',zipfile.ZIP_DEFLATED) as zf:
                zf.writestr('Punpun.toml','[package]\nname="real_name"\nversion="1.0.0"\n')
                zf.writestr('src/main.pp','launch {}\n')
            raw=buf.getvalue()
            with self.assertRaises(ValueError):
                registry.publish(user,{'name':'spoofed','version':'1.0.0','dependencies':{},
                                       'checksum':hashlib.sha256(raw).hexdigest(),'archive_b64':base64.b64encode(raw).decode()})


class PpxRegistryIntegrationTests(unittest.TestCase):
    def test_search_add_and_build_from_local_registry(self):
        registry_mod=load_module(ROOT/'ppx-registry'/'server.py', 'punpun_registry_http_test')
        with tempfile.TemporaryDirectory() as td, tempfile.TemporaryDirectory() as cache_td, tempfile.TemporaryDirectory() as project_td:
            registry=registry_mod.Registry(Path(td)/'registry')
            registry.register('publisher','abcdefgh')
            token=registry.login('publisher','abcdefgh')
            user=registry.authenticate(token)

            package_root=ROOT/'packages'/'requests'
            buf=io.BytesIO()
            with zipfile.ZipFile(buf,'w',zipfile.ZIP_DEFLATED) as zf:
                for path in sorted(package_root.rglob('*')):
                    if path.is_file(): zf.write(path,path.relative_to(package_root).as_posix())
            raw=buf.getvalue(); checksum=hashlib.sha256(raw).hexdigest()
            registry.publish(user,{
                'name':'requests','version':'0.1.0','description':'registry requests',
                'dependencies':{},'checksum':checksum,'archive_b64':base64.b64encode(raw).decode(),
            })

            registry_mod.Handler.registry=registry
            server=registry_mod.ThreadingHTTPServer(('127.0.0.1',0),registry_mod.Handler)
            thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
            self.addCleanup(server.shutdown); self.addCleanup(server.server_close)
            url=f'http://127.0.0.1:{server.server_address[1]}'

            root=Path(project_td)
            run_checked([str(PP),'init','registry_app'],cwd=root)
            env=os.environ.copy()
            env['PPX_REGISTRY']=url
            env['PUNPUN_PP']=str(PP)
            env['PUNPUN_PACKAGES']=str(Path(td)/'no-bundled-packages')
            env['XDG_CACHE_HOME']=cache_td
            search=run_checked(['python3',str(PPX),'search','requests'],cwd=root,env=env)
            self.assertIn('requests',search.stdout)
            self.assertIn('registry',search.stdout)
            added=run_checked(['python3',str(PPX),'add','requests'],cwd=root,env=env)
            self.assertIn('PPX added requests',added.stdout)
            (root/'src/main.pp').write_text('bring requests;\nlaunch { say(requests_available()); }\n')
            run=run_checked([str(PP),'run'],cwd=root,env=env)
            self.assertRegex(run.stdout.strip(),r'^(yes|no)$')
            cached=list((Path(cache_td)/'ppx'/'packages'/'requests'/'0.1.0').rglob('.ppx-checksum'))
            self.assertEqual(len(cached),1)

    def test_cli_register_publish_download_and_install(self):
        registry_mod=load_module(ROOT/'ppx-registry'/'server.py', 'punpun_registry_publish_http_test')
        with tempfile.TemporaryDirectory() as td, tempfile.TemporaryDirectory() as state_td, tempfile.TemporaryDirectory() as cache_td, tempfile.TemporaryDirectory() as package_td, tempfile.TemporaryDirectory() as project_td:
            registry=registry_mod.Registry(Path(td)/'registry')
            registry_mod.Handler.registry=registry
            server=registry_mod.ThreadingHTTPServer(('127.0.0.1',0),registry_mod.Handler)
            thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
            self.addCleanup(server.shutdown); self.addCleanup(server.server_close)
            url=f'http://127.0.0.1:{server.server_address[1]}'
            env=os.environ.copy(); env.update({
                'PPX_REGISTRY':url,'XDG_CONFIG_HOME':state_td,'XDG_CACHE_HOME':cache_td,
                'PUNPUN_PP':str(PP),'PUNPUN_PACKAGES':str(Path(td)/'no-bundled')
            })
            package=Path(package_td); (package/'src').mkdir(); (package/'README.md').write_text('# Demo package\n')
            (package/'Punpun.toml').write_text(
                '[package]\nname="upload_demo"\nversion="1.0.0"\ndescription="upload integration"\n'
                'license="MIT"\nreadme="README.md"\nkeywords=["demo"]\nentry="src/main.pp"\n\n[dependencies]\n'
            )
            (package/'src/main.pp').write_text('fn answer() -> i64 { return 42; }\n')
            run_checked(['python3',str(PPX),'register','publisher','--password','abcdefgh'],cwd=package,env=env)
            run_checked(['python3',str(PPX),'login','publisher','--password','abcdefgh'],cwd=package,env=env)
            dry=run_checked(['python3',str(PPX),'publish','--dry-run'],cwd=package,env=env)
            self.assertIn('nothing uploaded',dry.stdout)
            published=run_checked(['python3',str(PPX),'upload'],cwd=package,env=env)
            self.assertIn('published upload_demo 1.0.0',published.stdout)
            metadata=run_checked(['python3',str(PPX),'info','upload_demo'],cwd=package,env=env)
            self.assertIn('"license": "MIT"',metadata.stdout)
            archive=Path(td)/'downloaded.zip'
            run_checked(['python3',str(PPX),'download','upload_demo','1.0.0','-o',str(archive)],cwd=package,env=env)
            self.assertTrue(archive.is_file())
            with zipfile.ZipFile(archive) as zf: self.assertIn('Punpun.toml',zf.namelist())
            project=Path(project_td); run_checked([str(PP),'init','consumer'],cwd=project,env=env)
            installed=run_checked(['python3',str(PPX),'install','upload_demo','1.0.0'],cwd=project,env=env)
            self.assertIn('PPX added upload_demo',installed.stdout)
            self.assertIn('upload_demo', (project/'Punpun.toml').read_text())

class FirstPartyPackageTests(unittest.TestCase):
    def make_project(self, td):
        run_checked([str(PP),'init','ecosystem_test'],cwd=td)
        return Path(td)

    def add(self, root, name):
        env=os.environ.copy(); env['PUNPUN_PP']=str(PP); env['PUNPUN_PACKAGES']=str(ROOT/'packages')
        return run_checked(['python3',str(PPX),'add',name],cwd=root,env=env)

    def run_project(self, root):
        return run_checked([str(PP),'run'],cwd=root).stdout

    def test_logging_package_compiles_and_runs(self):
        with tempfile.TemporaryDirectory() as td:
            root=self.make_project(td); self.add(root,'logging')
            (root/'src/main.pp').write_text('bring logging;\nlaunch { log_info("package ok"); }\n')
            self.assertIn('[info] package ok',self.run_project(root))

    def test_json_package_compiles_and_runs(self):
        with tempfile.TemporaryDirectory() as td:
            root=self.make_project(td); self.add(root,'json')
            (root/'src/main.pp').write_text('bring json;\nlaunch { let text = "{\\\"name\\\":\\\"PunPun\\\",\\\"n\\\":42}"; say(json_valid(text)); say(json_get_string(text,"name","?")); say(json_get_i64(text,"n",0)); }\n')
            out=self.run_project(root)
            self.assertIn('yes',out); self.assertIn('PunPun',out); self.assertIn('42',out)

    def test_requests_package_gets_from_local_server_and_async_awaits(self):
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                body=b"punpun-http-ok"
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            def log_message(self, fmt, *args):
                pass

        server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Handler)
        thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
        self.addCleanup(server.shutdown); self.addCleanup(server.server_close)
        url=f"http://127.0.0.1:{server.server_address[1]}/hello"
        with tempfile.TemporaryDirectory() as td:
            root=self.make_project(td); self.add(root,'requests')
            (root/'src/main.pp').write_text(
                'bring requests;\n'
                'async fn fetch(url: String) -> String { let r = requests_get(url); return r.body; }\n'
                f'launch {{ let task = fetch("{url}"); let body = await task; say(body); }}\n'
            )
            out=self.run_project(root)
            self.assertIn('punpun-http-ok',out)

    def test_requests_package_verbs_headers_and_head(self):
        class Handler(http.server.BaseHTTPRequestHandler):
            def reply(self):
                length=int(self.headers.get('Content-Length','0'))
                request_body=self.rfile.read(length).decode() if length else ''
                body=f'{self.command}|{self.headers.get("X-PunPun","")}|{request_body}'.encode()
                self.send_response(201)
                self.send_header('Content-Length',str(len(body)))
                self.end_headers()
                if self.command!='HEAD': self.wfile.write(body)
            do_PUT=reply
            do_PATCH=reply
            do_DELETE=reply
            do_HEAD=reply
            def log_message(self,fmt,*args): pass

        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
        thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
        self.addCleanup(server.shutdown); self.addCleanup(server.server_close)
        url=f'http://127.0.0.1:{server.server_address[1]}/resource'
        with tempfile.TemporaryDirectory() as td:
            root=self.make_project(td); self.add(root,'requests')
            (root/'src/main.pp').write_text(
                'bring requests;\n'
                f'launch {{ let put = requests_request("PUT","{url}","payload","X-PunPun: yes",5000,false); say(put.status); say(put.body); let patch = requests_patch("{url}","edit"); say(patch.body); let deletion = requests_delete("{url}"); say(deletion.body); let head = requests_head("{url}"); say(head.status); say(len(head.body)); }}\n'
            )
            out=self.run_project(root).splitlines()
            self.assertEqual(out,['201','PUT|yes|payload','PATCH||edit','DELETE||','201','0'])

    def test_requests_package_compiles_and_error_path_is_structured(self):
        with tempfile.TemporaryDirectory() as td:
            root=self.make_project(td); self.add(root,'requests')
            (root/'src/main.pp').write_text('bring requests;\nlaunch { say(requests_available()); let r = requests_get("http://127.0.0.1:1/"); say(r.status); say(r.error); }\n')
            out=self.run_project(root)
            self.assertRegex(out.splitlines()[0], r'^(yes|no)$')
            self.assertGreaterEqual(len(out.splitlines()),3)


class WebsiteBuildTests(unittest.TestCase):
    def test_static_sites_build(self):
        for site in ('docs-site','ppx-site'):
            proc=subprocess.run(['python3','build.py'],cwd=ROOT/site,text=True,capture_output=True)
            self.assertEqual(proc.returncode,0,proc.stderr)
            self.assertTrue((ROOT/site/'dist'/'index.html').is_file())

    def test_ppx_public_site_has_offline_catalog(self):
        subprocess.run(['python3','build.py'],cwd=ROOT/'ppx-site',check=True,text=True,capture_output=True)
        catalog=(ROOT/'ppx-site'/'dist'/'static'/'catalog.json').read_text()
        site_js=(ROOT/'ppx-site'/'dist'/'static'/'site.js').read_text()
        index=(ROOT/'ppx-site'/'dist'/'index.html').read_text()
        self.assertIn('"requests"',catalog)
        self.assertIn('"json"',catalog)
        self.assertNotIn('127.0.0.1',site_js)
        self.assertNotIn('Local registry not running',site_js)
        self.assertIn('../punpun-docs/',index)
        self.assertTrue((ROOT/'ppx-site'/'dist'/'README.md').is_file())

    def test_gui_designer_is_self_contained(self):
        self.assertTrue((ROOT/'gui-maker'/'index.html').is_file())
        self.assertTrue((ROOT/'gui-maker'/'designer.js').is_file())
        self.assertIn('Copy PunPun',(ROOT/'gui-maker'/'index.html').read_text())

if __name__=='__main__': unittest.main()
