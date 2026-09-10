from __future__ import annotations
from pathlib import Path
from PySide6.QtCore import QThread, Signal
from PySide6.QtWidgets import QDialog, QVBoxLayout, QLabel, QPushButton, QProgressBar, QMessageBox
from .toolchain import fetch_latest_release, select_release_asset, install_release_asset, Toolchain

class InstallWorker(QThread):
    progress=Signal(int); done=Signal(str); failed=Signal(str)
    def __init__(self, app_dir: Path): super().__init__(); self.app_dir=app_dir
    def run(self):
        try:
            rel=fetch_latest_release(); asset=select_release_asset(rel)
            if not asset: raise RuntimeError("No prebuilt PunPun release is available for this OS/CPU. macOS and non-x86_64 currently require a source build.")
            def p(a,b): self.progress.emit(int(a*100/b) if b else 0)
            install_release_asset(asset,self.app_dir,p); self.done.emit(rel.get("tag_name","installed"))
        except Exception as e: self.failed.emit(str(e))

class ToolchainDialog(QDialog):
    def __init__(self, toolchain: Toolchain, parent=None):
        super().__init__(parent); self.tc=toolchain; self.setWindowTitle("PunPun Toolchain"); self.resize(470,210)
        lay=QVBoxLayout(self); self.title=QLabel("PunPun 1.3 Toolchain"); self.title.setStyleSheet("font-size:20px;font-weight:600"); lay.addWidget(self.title)
        self.state=QLabel(); lay.addWidget(self.state); self.note=QLabel("Installs privately inside ~/.punpun-ide. Your global PATH is not changed."); self.note.setWordWrap(True); lay.addWidget(self.note)
        self.bar=QProgressBar(); self.bar.hide(); lay.addWidget(self.bar)
        self.install=QPushButton("Download / Update PunPun"); self.install.clicked.connect(self.start); lay.addWidget(self.install); self.refresh()
    def refresh(self): self.state.setText("Installed: "+(self.tc.version() or "not found"))
    def start(self):
        self.install.setEnabled(False); self.bar.show(); self.worker=InstallWorker(self.tc.app_dir); self.worker.progress.connect(self.bar.setValue); self.worker.done.connect(self.finished_ok); self.worker.failed.connect(self.failed); self.worker.start()
    def finished_ok(self,tag): self.install.setEnabled(True); self.bar.setValue(100); self.refresh(); QMessageBox.information(self,"PunPun",f"PunPun {tag} is ready for this IDE.")
    def failed(self,msg): self.install.setEnabled(True); self.bar.hide(); QMessageBox.critical(self,"Toolchain install failed",msg)
