import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]

class DesktopIntegrationTests(unittest.TestCase):
    def test_vscode_language_icon_and_extension_association(self):
        package=json.loads((ROOT/'editors/vscode/package.json').read_text())
        language=package['contributes']['languages'][0]
        self.assertIn('.pp', language['extensions'])
        self.assertEqual(language['id'],'punpun')
        icon=language['icon']
        for mode in ('light','dark'):
            path=(ROOT/'editors/vscode'/icon[mode]).resolve()
            self.assertTrue(path.is_file(),f'missing VS Code {mode} language icon: {path}')

    def test_linux_mime_mapping_and_user_install_uninstall(self):
        xml_path=ROOT/'packaging/linux/application-x-punpun.xml'
        tree=ET.parse(xml_path)
        ns={'m':'http://www.freedesktop.org/standards/shared-mime-info'}
        mime=tree.getroot().find('m:mime-type',ns)
        self.assertIsNotNone(mime)
        self.assertEqual(mime.attrib['type'],'application/x-punpun')
        glob=mime.find('m:glob',ns)
        self.assertEqual(glob.attrib['pattern'],'*.pp')
        with tempfile.TemporaryDirectory() as td:
            data=Path(td)/'share'
            env=os.environ.copy(); env['PUNPUN_DATA_ROOT']=str(data)
            subprocess.run([str(ROOT/'packaging/linux/install-file-icons.sh'),'--system'],check=True,env=env,capture_output=True,text=True)
            self.assertTrue((data/'mime/packages/punpun.xml').is_file())
            for size in (16,32,64,128,256,512):
                self.assertTrue((data/f'icons/hicolor/{size}x{size}/mimetypes/application-x-punpun.png').is_file())
            subprocess.run([str(ROOT/'packaging/linux/uninstall-file-icons.sh'),'--system'],check=True,env=env,capture_output=True,text=True)
            self.assertFalse((data/'mime/packages/punpun.xml').exists())

    def test_windows_uses_real_ico_file_type_icon(self):
        ico=ROOT/'assets/punpun-source.ico'
        self.assertTrue(ico.is_file())
        self.assertEqual(ico.read_bytes()[:4],b'\x00\x00\x01\x00')
        wix=(ROOT/'installers/windows/wix/PunPun.wxs').read_text()
        self.assertIn('Software\\Classes\\.pp',wix)
        self.assertIn('PunPun.Source\\DefaultIcon',wix)
        self.assertIn('punpun-source.ico',wix)

if __name__=='__main__': unittest.main()
