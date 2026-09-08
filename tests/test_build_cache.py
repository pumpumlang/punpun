#!/usr/bin/env python3
import os
import shutil
import subprocess
import tempfile
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PP = os.path.join(ROOT, "pp")


class BuildCacheRegressionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.mkdtemp(prefix="punpun-cache-")
        self.addCleanup(shutil.rmtree, self.temp, ignore_errors=True)
        created = subprocess.run([PP, "new", "app"], cwd=self.temp, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
        self.assertEqual(created.returncode, 0, created.stderr)
        self.project = os.path.join(self.temp, "app")
        self.source = os.path.join(self.project, "src", "main.pp")

    def run_pp(self, *args):
        return subprocess.run([PP, *args], cwd=self.project, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)

    def write_source(self, text):
        # Preserve the file timestamp deliberately. Cache correctness must depend
        # on content, not filesystem timestamp resolution.
        old = os.stat(self.source) if os.path.exists(self.source) else None
        with open(self.source, "w", encoding="utf-8") as handle:
            handle.write(text)
        if old:
            os.utime(self.source, ns=(old.st_atime_ns, old.st_mtime_ns))

    def test_edited_source_rebuilds_even_with_unchanged_timestamp(self):
        self.write_source('launch {\n    say("VERSION A");\n}\n')
        first = self.run_pp("run")
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertIn("VERSION A", first.stdout)

        self.write_source('launch {\n    say("VERSION B");\n}\n')
        second = self.run_pp("run", "--cache-info")
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertIn("VERSION B", second.stdout)
        self.assertNotIn("VERSION A", second.stdout)
        self.assertIn("CACHE MISS", second.stderr)

        third = self.run_pp("run", "--cache-info")
        self.assertEqual(third.returncode, 0, third.stderr)
        self.assertIn("VERSION B", third.stdout)
        self.assertIn("CACHE HIT", third.stderr)

    def test_dependency_edit_invalidates_root_build(self):
        lib = os.path.join(self.temp, "lib")
        subprocess.check_call([PP, "new", "lib"], cwd=self.temp,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        lib_source = os.path.join(lib, "src", "main.pp")
        with open(lib_source, "w", encoding="utf-8") as handle:
            handle.write('fn dep_value() -> i64 { return 10; }\n')
        added = self.run_pp("add", "lib", "../lib")
        self.assertEqual(added.returncode, 0, added.stderr)
        self.write_source('bring lib;\nlaunch { say(dep_value()); }\n')
        first = self.run_pp("run")
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertIn("10", first.stdout)

        old = os.stat(lib_source)
        with open(lib_source, "w", encoding="utf-8") as handle:
            handle.write('fn dep_value() -> i64 { return 11; }\n')
        os.utime(lib_source, ns=(old.st_atime_ns, old.st_mtime_ns))
        second = self.run_pp("run", "--cache-info")
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertIn("11", second.stdout)
        self.assertIn("CACHE MISS", second.stderr)

    def test_failed_rebuild_keeps_last_good_executable_but_run_does_not_execute_it(self):
        self.write_source('launch { say("GOOD"); }\n')
        build = self.run_pp("build")
        self.assertEqual(build.returncode, 0, build.stderr)
        exe = os.path.join(self.project, ".punpun", "bin", "main")
        with open(exe, "rb") as handle:
            before = handle.read()

        self.write_source('launch { say("BROKEN") } !!!\n')
        failed = self.run_pp("run")
        self.assertNotEqual(failed.returncode, 0)
        self.assertNotIn("GOOD", failed.stdout)
        with open(exe, "rb") as handle:
            after = handle.read()
        self.assertEqual(before, after, "failed compilation must not corrupt the last good executable")

    def test_toolchain_change_invalidates_cache(self):
        self.write_source('launch { say("TOOLCHAIN"); }\n')
        first = self.run_pp("build", "--toolchain", "gcc")
        self.assertEqual(first.returncode, 0, first.stderr)
        second = self.run_pp("build", "--toolchain", "gcc", "--cache-info")
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertIn("CACHE HIT", second.stderr)
        if shutil.which("clang"):
            third = self.run_pp("build", "--toolchain", "clang", "--cache-info")
            self.assertEqual(third.returncode, 0, third.stderr)
            self.assertIn("CACHE MISS", third.stderr)

    def test_stats_report_is_real_and_nonempty(self):
        self.write_source('launch { say("STATS"); }\n')
        result = self.run_pp("build", "--stats")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("stats files", result.stderr)
        self.assertIn("timing load+parse", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
