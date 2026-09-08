from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
PPC=ROOT/'build'/'ppc'
LEARNING_PAGES=['index','getting-started','language','functions','control-flow','objects','generics-results','memory']
REQUIRED_SECTIONS=['## Overview','## Syntax','## Runnable example','## Common mistakes','## Next steps']

class LearningDocumentationTests(unittest.TestCase):
    def test_core_learning_pages_share_structure_and_examples_compile(self):
        self.assertTrue(PPC.is_file(), 'build/ppc is required; run make compiler first')
        for slug in LEARNING_PAGES:
            with self.subTest(page=slug):
                text=(ROOT/'docs-site/content'/f'{slug}.md').read_text(encoding='utf-8')
                for heading in REQUIRED_SECTIONS:
                    self.assertIn(heading,text)
                runnable=text.split('## Runnable example',1)[1].split('## Common mistakes',1)[0]
                match=re.search(r'```(?:punpun|pp)\n(.*?)\n```',runnable,re.S)
                self.assertIsNotNone(match,'Runnable example must include a complete PunPun code block')
                with tempfile.TemporaryDirectory() as td:
                    path=Path(td)/f'{slug}.pp'; path.write_text(match.group(1)+'\n',encoding='utf-8')
                    result=subprocess.run([str(PPC),'check',str(path)],text=True,capture_output=True)
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=='__main__': unittest.main()
