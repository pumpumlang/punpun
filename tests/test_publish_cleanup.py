from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import release as release_mod

class PublishCleanupTests(unittest.TestCase):
    def test_clean_source_excludes_generated_and_host_binary_debris(self):
        with tempfile.TemporaryDirectory() as td:
            destination=Path(td)/'source'
            release_mod.copy_clean_source(destination)
            forbidden_dirs={'build','dist','__pycache__','.pytest_cache','.mypy_cache','.ruff_cache','node_modules','.idea','.punpun','.ppx-registry','htmlcov'}
            self.assertFalse(any(path.is_dir() and path.name in forbidden_dirs for path in destination.rglob('*')))
            forbidden_suffixes={'.o','.a','.so','.dll','.exe','.pyc','.tmp','.swp','.swo','.bak','.orig','.rej'}
            self.assertFalse(any(path.is_file() and path.suffix.lower() in forbidden_suffixes for path in destination.rglob('*')))
            self.assertFalse(any('.bak-' in path.name.lower() for path in destination.rglob('*') if path.is_file()))

    def test_publisher_prunes_only_current_release_assets_and_sanitizes_temp_trees(self):
        script=(ROOT/'publish-punpun.sh').read_text()
        self.assertIn('prune_release_assets',script)
        self.assertIn('gh release delete-asset "$tag" "$current"',script)
        self.assertIn('find -P "$tree"',script)
        self.assertIn('replace_checkout_contents',script)
        self.assertIn('intentionally retained:',script)
        self.assertIn("-name '*.bak-*'",script)
        self.assertIn('ensure_candidate_tag',script)
        self.assertIn('wait_for_platform_qualification',script)
        self.assertIn('gh run watch',script)
        self.assertLess(script.index('wait_for_platform_qualification "$SOURCE_SHA"'), script.index('step "Promoting release downloads"'))

if __name__=='__main__': unittest.main()
