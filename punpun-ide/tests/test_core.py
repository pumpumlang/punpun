import json, unittest
from punpun_ide.filetypes import kind_for
from punpun_ide.smart import fallback_problems, outline, parse_ppc_json
from punpun_ide.toolchain import select_release_asset

class CoreTests(unittest.TestCase):
    def test_file_types(self):
        self.assertEqual(kind_for('a.pp').id,'punpun'); self.assertEqual(kind_for('a.cpp').id,'cpp'); self.assertEqual(kind_for('a.hpp').id,'header'); self.assertEqual(kind_for('README.md').id,'markdown')
    def test_outline_punpun(self):
        rows=outline('fn add(a: int, b: int) -> int {\n return a+b;\n}\nstruct User {','punpun')
        self.assertEqual([(x.name,x.kind) for x in rows],[('add','function'),('User','type')])
    def test_fallback_bracket_check(self):
        p=fallback_problems('launch {\n say("hi");\n')
        self.assertTrue(any('Unclosed' in x.message for x in p))
    def test_ppc_json(self):
        p=parse_ppc_json(json.dumps({'message':'bad type','severity':'error','code':'E0800','line':4,'column':2}))
        self.assertEqual((p[0].code,p[0].line),('E0800',4))
    def test_release_selection(self):
        rel={'assets':[{'name':'PunPun-1.3.0-windows-x86_64.zip','browser_download_url':'w','size':1},{'name':'PunPun-1.3.0-linux-x86_64.tar.gz','browser_download_url':'l','size':2}]}
        self.assertEqual(select_release_asset(rel,'Windows','AMD64').url,'w'); self.assertEqual(select_release_asset(rel,'Linux','x86_64').url,'l'); self.assertIsNone(select_release_asset(rel,'Darwin','arm64'))

if __name__=='__main__': unittest.main()
