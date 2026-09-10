from pathlib import Path
import subprocess, sys
root=Path(__file__).resolve().parents[1]
assets=root/"assets"
sep=';' if sys.platform.startswith('win') else ':'
subprocess.check_call([sys.executable,"-m","PyInstaller","--noconfirm","--clean","--windowed","--name","PunPunIDE","--add-data",f"{assets}{sep}assets",str(root/"run_ide.py")],cwd=root)
