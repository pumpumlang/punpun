#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PPX_ROOT = Path(os.environ.get("PUNPUN_PPX_ROOT", ROOT.parent / "punpun-ppx"))
PPX = PPX_ROOT / "ppx" / "ppx"


class PPXIntegrationTests(unittest.TestCase):
    def test_launcher_passes_materialized_dependency_roots(self):
        self.assertTrue(PPX.is_file(), "matching punpun-ppx checkout is required")
        with tempfile.TemporaryDirectory() as td:
            workspace = Path(td)
            dependency = workspace / "feature"
            project = workspace / "app"
            dependency.mkdir()
            project.mkdir()
            (dependency / "feature.pp").write_text(
                "fn feature_value() -> int { return 42; }\n",
                encoding="utf-8",
            )
            (project / "Punpun.toml").write_text(
                '[package]\nname = "app"\nversion = "0.1.0"\n\n'
                '[dependencies]\nfeature = { path = "../feature" }\n',
                encoding="utf-8",
            )
            (project / "main.pp").write_text(
                "import feature;\nlaunch { say(feature_value()); }\n",
                encoding="utf-8",
            )
            env = os.environ.copy()
            env["PUNPUN_PPX"] = str(PPX)
            result = subprocess.run(
                [str(ROOT / "pp"), "check", "main.pp"],
                cwd=project,
                env=env,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("function(s) checked", result.stdout)


if __name__ == "__main__":
    unittest.main()
