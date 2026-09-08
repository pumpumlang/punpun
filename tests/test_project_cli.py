#!/usr/bin/env python3
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PP = os.path.join(ROOT, "pp")


class ProjectCliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.mkdtemp(prefix="punpun-project-")
        self.addCleanup(shutil.rmtree, self.temp, ignore_errors=True)

    def run_pp(self, *args, cwd=None):
        return subprocess.run([PP, *args], cwd=cwd or self.temp, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)

    def test_new_build_run_and_clean(self):
        created = self.run_pp("new", "comet")
        self.assertEqual(created.returncode, 0, created.stderr)
        project = os.path.join(self.temp, "comet")
        self.assertTrue(os.path.isfile(os.path.join(project, "Punpun.toml")))
        self.assertTrue(os.path.isfile(os.path.join(project, "src", "main.pp")))
        run = self.run_pp("run", cwd=project)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn("hello from comet", run.stdout)
        build = self.run_pp("build", cwd=project)
        self.assertEqual(build.returncode, 0, build.stderr)
        self.assertTrue(os.path.isfile(os.path.join(project, ".punpun", "bin", "main")))
        clean = self.run_pp("clean", cwd=project)
        self.assertEqual(clean.returncode, 0, clean.stderr)
        self.assertFalse(os.path.exists(os.path.join(project, ".punpun")))

    def test_test_command_runs_pp_programs(self):
        self.assertEqual(self.run_pp("init", "suite").returncode, 0)
        tests = os.path.join(self.temp, "tests")
        os.makedirs(tests, exist_ok=True)
        with open(os.path.join(tests, "math.pp"), "w", encoding="utf-8") as handle:
            handle.write('launch:\n    assert(2 + 2 == 4, "math")\ndone\n')
        result = self.run_pp("test")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("1/1 PunPun tests passed", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
