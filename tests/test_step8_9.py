import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PPC = ROOT / "build" / "ppc"
PP = ROOT / "pp"
PPX = ROOT / "ppx" / "ppx.py"


def run(args, *, cwd=ROOT, env=None, timeout=60):
    result = subprocess.run([str(x) for x in args], cwd=cwd, env=env, text=True,
                            capture_output=True, timeout=timeout)
    if result.returncode != 0:
        raise AssertionError(
            f"command failed ({result.returncode}): {' '.join(map(str,args))}\n"
            f"--- stdout ---\n{result.stdout}\n--- stderr ---\n{result.stderr}"
        )
    return result


class StructuredAsyncTests(unittest.TestCase):
    def test_task_group_runs_on_direct_and_portable_c(self):
        case = ROOT / "tests" / "fixtures" / "language_0_8" / "task_group_run.pp"
        for flags in ([], ["--cc-backend"]):
            result = run([PPC, "run", case, "--no-cache", *flags])
            self.assertEqual(result.stdout.strip(), "42")

    def test_task_group_cancellation_wakes_sleep(self):
        source = '''
async fn worker() -> i64 {
    sleep_ms(5000);
    if cancelled() { return 7; }
    return 0;
}
launch {
    let g = task_group();
    let t = worker();
    task_group_add(g, t);
    task_group_cancel(g);
    assert(task_group_wait_for(g, 500), "cancelled task should finish promptly");
    assert(await t == 7, "worker observed cancellation");
    task_group_close(g);
    say("cancel-ok");
}
'''
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "cancel.pp"
            path.write_text(source)
            result = run([PPC, "run", path, "--no-cache"], timeout=10)
            self.assertIn("cancel-ok", result.stdout)


class DebugAndDocsTests(unittest.TestCase):
    def test_debug_map_contains_source_locations(self):
        result = run(["python3", ROOT / "scripts" / "debug_map.py",
                      ROOT / "examples" / "hello" / "main.pp", "--ppc", PPC])
        payload = json.loads(result.stdout)
        self.assertEqual(payload["format"], 1)
        self.assertTrue(payload["entries"])
        self.assertTrue(any(entry["function"] == "pp_fn_main" for entry in payload["entries"]))

    def test_generated_api_docs_are_current_and_doctests_run(self):
        run(["python3", ROOT / "scripts" / "docgen.py", "--check"])
        result = run(["python3", ROOT / "scripts" / "doctest.py", "--ppc", PPC])
        self.assertIn("doctest(s) passed", result.stdout)


class HardeningToolTests(unittest.TestCase):
    def test_frontend_fuzz_smoke(self):
        result = run(["python3", ROOT / "scripts" / "fuzz_frontend.py",
                      "--iterations", "12", "--seed", "99", "--ppc", PPC])
        self.assertIn("mutation cases passed", result.stdout)

    def test_backend_compatibility_smoke(self):
        result = run(["python3", ROOT / "scripts" / "compat_matrix.py", "--quick", "--ppc", PPC])
        self.assertIn("compat matrix", result.stdout)

    def test_async_stress_smoke(self):
        result = run(["python3", ROOT / "scripts" / "stress.py", "--quick", "--ppc", PPC])
        self.assertIn("iterations passed", result.stdout)

    @unittest.skipUnless(shutil.which("cc"), "C compiler unavailable")
    def test_pgo_pipeline_builds_runnable_binary(self):
        with tempfile.TemporaryDirectory() as td:
            output = Path(td) / "hello-pgo"
            run(["python3", ROOT / "scripts" / "pgo.py", ROOT / "examples" / "hello" / "main.pp",
                 "-o", output], timeout=90)
            result = run([output])
            self.assertIn("fibonacci(10) = 55", result.stdout)


class PpxIntegrityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("ppx_client_step9", PPX)
        cls.ppx = importlib.util.module_from_spec(spec)
        assert spec.loader
        spec.loader.exec_module(cls.ppx)

    def test_archive_contains_and_verifies_integrity_manifest(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "src").mkdir()
            (root / "Punpun.toml").write_text('[package]\nname="secure_demo"\nversion="1.0.0"\nentry="src/main.pp"\n\n[dependencies]\n')
            (root / "src" / "main.pp").write_text('fn answer() -> i64 { return 42; }\n')
            content, _ = self.ppx.create_package_archive(root)
            archive = root / "package.zip"
            archive.write_bytes(content)
            with zipfile.ZipFile(archive) as zf:
                self.assertIn("PPX-MANIFEST.json", zf.namelist())
            extracted = root / "extracted"
            self.ppx.safe_extract_zip(archive, extracted)
            manifest = self.ppx.verify_package_tree(extracted)
            self.assertGreaterEqual(len(manifest["files"]), 2)
            (extracted / "src" / "main.pp").write_text("tampered\n")
            with self.assertRaises(SystemExit):
                self.ppx.verify_package_tree(extracted)

    def test_nonloopback_http_registry_requires_explicit_opt_in(self):
        old = os.environ.get("PPX_REGISTRY")
        old_allow = os.environ.get("PPX_ALLOW_INSECURE_REGISTRY")
        try:
            os.environ["PPX_REGISTRY"] = "http://packages.example.test"
            os.environ.pop("PPX_ALLOW_INSECURE_REGISTRY", None)
            with self.assertRaises(SystemExit):
                self.ppx.registry_url()
            os.environ["PPX_ALLOW_INSECURE_REGISTRY"] = "1"
            self.assertEqual(self.ppx.registry_url(), "http://packages.example.test")
        finally:
            if old is None: os.environ.pop("PPX_REGISTRY", None)
            else: os.environ["PPX_REGISTRY"] = old
            if old_allow is None: os.environ.pop("PPX_ALLOW_INSECURE_REGISTRY", None)
            else: os.environ["PPX_ALLOW_INSECURE_REGISTRY"] = old_allow


if __name__ == "__main__":
    unittest.main(verbosity=2)
