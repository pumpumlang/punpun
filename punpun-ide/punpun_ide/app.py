from __future__ import annotations
import sys
from pathlib import Path
from PySide6.QtGui import QIcon
from PySide6.QtWidgets import QApplication
from .main_window import MainWindow
from .theme import stylesheet

def main(argv=None):
    argv=list(sys.argv if argv is None else argv); app=QApplication(argv); app.setApplicationName("PunPun IDE"); app.setOrganizationName("PunPun")
    mark=Path(__file__).resolve().parent.parent/"assets"/"branding"/"punpun-mark.svg"
    if mark.exists(): app.setWindowIcon(QIcon(str(mark)))
    app.setStyleSheet(stylesheet()); start=argv[1] if len(argv)>1 else None; win=MainWindow(start); win.show(); return app.exec()
