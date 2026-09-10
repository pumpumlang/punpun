from __future__ import annotations
from pathlib import Path
import os, subprocess, threading
from PySide6.QtCore import Qt, QDir, QProcess, QTimer, Signal, QObject
from PySide6.QtGui import QAction, QIcon, QTextCursor, QKeySequence, QShortcut
from PySide6.QtWidgets import (QMainWindow,QFileSystemModel,QTreeView,QTabWidget,QDockWidget,QPlainTextEdit,QListWidget,QListWidgetItem,QToolBar,QFileDialog,QMessageBox,QSplitter,QTextBrowser,QLineEdit,QWidget,QVBoxLayout,QLabel,QCompleter,QMenu,QToolTip)
from .editor import CodeEditor
from .filetypes import kind_for
from .toolchain import Toolchain
from .smart import fallback_problems, outline, help_for_word
from .downloader import ToolchainDialog
from .lsp import PunPunLspClient, LspError

class Bridge(QObject):
    diagnostics=Signal(str,object)

class MainWindow(QMainWindow):
    def __init__(self, start_path: str | None=None):
        super().__init__(); self.resize(1380,850); self.setWindowTitle("PunPun IDE"); self.tc=Toolchain(); self.root=Path(start_path or os.getcwd()).resolve(); self.lsp=None; self.bridge=Bridge(); self.bridge.diagnostics.connect(self.on_lsp_diagnostics); self._problems={}; self._checks={}
        self._build_ui(); self.open_root(self.root); self._maybe_start_lsp()
    def asset(self,name): return str(Path(__file__).resolve().parent.parent/"assets"/"icons"/name)
    def _build_ui(self):
        self.tabs=QTabWidget(); self.tabs.setTabsClosable(True); self.tabs.tabCloseRequested.connect(self.close_tab); self.tabs.currentChanged.connect(self.refresh_side_info); self.setCentralWidget(self.tabs)
        model=QFileSystemModel(self); model.setFilter(QDir.Filter.AllDirs|QDir.Filter.Files|QDir.Filter.NoDotAndDotDot); self.model=model
        self.tree=QTreeView(); self.tree.setModel(model); self.tree.doubleClicked.connect(self.tree_open); [self.tree.hideColumn(i) for i in (1,2,3)]
        dock=QDockWidget("Explorer",self); dock.setWidget(self.tree); self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea,dock)
        self.symbols=QListWidget(); self.symbols.itemActivated.connect(self.jump_symbol); sd=QDockWidget("Outline",self); sd.setWidget(self.symbols); self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea,sd)
        bottom=QTabWidget(); self.output=QPlainTextEdit(); self.output.setReadOnly(True); self.problems=QListWidget(); self.problems.itemActivated.connect(self.jump_problem); term=QWidget(); tl=QVBoxLayout(term); self.termout=QPlainTextEdit(); self.termout.setReadOnly(True); self.termin=QLineEdit(); self.termin.setPlaceholderText("Run a shell command in the project…"); self.termin.returnPressed.connect(self.run_terminal); tl.addWidget(self.termout); tl.addWidget(self.termin); bottom.addTab(self.problems,"Problems"); bottom.addTab(self.output,"Output"); bottom.addTab(term,"Terminal")
        bd=QDockWidget("Panel",self); bd.setWidget(bottom); self.addDockWidget(Qt.DockWidgetArea.BottomDockWidgetArea,bd); self.bottom=bottom
        bar=QToolBar("Main"); bar.setMovable(False); self.addToolBar(bar)
        def action(text,icon,slot,shortcut=None):
            a=QAction(QIcon(self.asset(icon)),text,self); a.triggered.connect(slot); a.setToolTip(text+(f" ({shortcut})" if shortcut else ""));
            if shortcut: a.setShortcut(shortcut)
            bar.addAction(a); return a
        action("Open Folder","folder.svg",self.choose_folder,"Ctrl+K, Ctrl+O"); action("Save","save.svg",self.save,"Ctrl+S"); bar.addSeparator(); action("Run","run-all.svg",self.run_current,"F5"); action("Debug","debug-alt.svg",self.debug_current,"F6"); action("Check","search.svg",self.check_current,"Ctrl+Shift+B"); bar.addSeparator(); action("PunPun Toolchain","settings.svg",self.toolchain_dialog)
        QShortcut(QKeySequence("Ctrl+Space"), self, activated=self.semantic_complete)
        QShortcut(QKeySequence("F1"), self, activated=self.semantic_hover)
        self.statusBar().showMessage("Ready")
    def open_root(self,path: Path): self.root=path; self.model.setRootPath(str(path)); self.tree.setRootIndex(self.model.index(str(path))); self.setWindowTitle(f"{path.name} · PunPun IDE")
    def choose_folder(self):
        p=QFileDialog.getExistingDirectory(self,"Open Folder",str(self.root));
        if p: self.open_root(Path(p)); self._restart_lsp()
    def tree_open(self,index):
        p=Path(self.model.filePath(index));
        if p.is_file(): self.open_file(p)
    def open_file(self,path: Path):
        for i in range(self.tabs.count()):
            e=self.tabs.widget(i)
            if getattr(e,"path",None)==path: self.tabs.setCurrentIndex(i); return
        if kind_for(path).id=="markdown":
            split=QSplitter(); ed=CodeEditor(path); prev=QTextBrowser(); split.addWidget(ed); split.addWidget(prev); split.editor=ed; split.path=path; ed.preview=prev; ed.textChanged.connect(lambda: prev.setMarkdown(ed.toPlainText())); ed.changed.connect(self.editor_changed); widget=split
        else:
            ed=CodeEditor(path); ed.changed.connect(self.editor_changed); widget=ed
        try: text=path.read_text(encoding="utf-8")
        except UnicodeDecodeError: QMessageBox.warning(self,"Unsupported file","This file is not UTF-8 text."); return
        ed.blockSignals(True); ed.setPlainText(text); ed.blockSignals(False); ed.cursorPositionChanged.connect(lambda ed=ed:self.update_context_help(ed))
        if hasattr(ed,"preview"): ed.preview.setMarkdown(text)
        idx=self.tabs.addTab(widget,path.name); self.tabs.setCurrentIndex(idx)
        if kind_for(path).id=="punpun" and self.lsp:
            try: self.lsp.open_document(path,text,ed.version)
            except Exception: pass
        self.refresh_side_info()
    def current_editor(self):
        w=self.tabs.currentWidget(); return getattr(w,"editor",w) if w else None
    def close_tab(self,i):
        w=self.tabs.widget(i); e=getattr(w,"editor",w); p=getattr(e,"path",None)
        if p and kind_for(p).id=="punpun" and self.lsp:
            try: self.lsp.close_document(p)
            except Exception: pass
        self.tabs.removeTab(i)
    def save(self):
        e=self.current_editor();
        if not e: return
        e.path.write_text(e.toPlainText(),encoding="utf-8"); e.document().setModified(False); self.statusBar().showMessage(f"Saved {e.path.name}",2000)
    def append_output(self,text): self.output.moveCursor(QTextCursor.MoveOperation.End); self.output.insertPlainText(text); self.output.moveCursor(QTextCursor.MoveOperation.End)
    def _run_process(self,argv,cwd,after=None,interactive=False):
        target=self.termout if interactive else self.output
        self.bottom.setCurrentWidget(target.parentWidget() if interactive else self.output)
        if interactive: self.termout.appendPlainText("\n$ "+" ".join(map(str,argv)))
        else: self.append_output("\n$ "+" ".join(map(str,argv))+"\n")
        p=QProcess(self); p.setWorkingDirectory(str(cwd)); env=self.tc.env(); from PySide6.QtCore import QProcessEnvironment; pe=QProcessEnvironment.systemEnvironment(); [pe.insert(k,v) for k,v in env.items()]; p.setProcessEnvironment(pe); p.readyReadStandardOutput.connect(lambda:(self.termout.appendPlainText(bytes(p.readAllStandardOutput()).decode(errors="replace").rstrip()) if interactive else self.append_output(bytes(p.readAllStandardOutput()).decode(errors="replace")))); p.readyReadStandardError.connect(lambda:(self.termout.appendPlainText(bytes(p.readAllStandardError()).decode(errors="replace").rstrip()) if interactive else self.append_output(bytes(p.readAllStandardError()).decode(errors="replace")))); p.finished.connect(lambda code,status:((self.termout.appendPlainText(f"[exit {code}]") if interactive else self.append_output(f"\n[exit {code}]\n")),after(code) if after else None)); p.start(str(argv[0]),[str(x) for x in argv[1:]]); self._process=p
        if interactive: self._interactive_process=p
    def run_current(self):
        e=self.current_editor();
        if not e: return
        self.save()
        try: plan=self.tc.run_plan(e.path)
        except Exception as ex: return self._tool_error(ex)
        kind=kind_for(e.path)
        if kind.id in {"c","cpp"}:
            out=self.tc.native_output_for(e.path); self._run_process(plan.argv,plan.cwd,lambda code:self._run_process([str(out)],plan.cwd) if code==0 else None)
        else: self._run_process(plan.argv,plan.cwd)
    def debug_current(self):
        e=self.current_editor();
        if not e: return
        self.save()
        try: plan,dbg=self.tc.debug_plan(e.path)
        except Exception as ex: return self._tool_error(ex)
        if kind_for(e.path).id=="punpun" and Path(plan.argv[0]).name.lower() in {"pp","punpun","pp.exe","punpun.exe"}: self._run_process(plan.argv,plan.cwd,interactive=True); return
        self._run_process(plan.argv,plan.cwd,lambda code:self._run_process(dbg,plan.cwd,interactive=True) if code==0 and dbg else self.append_output("No GDB/LLDB found; debug executable was built.\n"))
    def check_current(self):
        e=self.current_editor();
        if not e: return
        if kind_for(e.path).id!="punpun": self.show_fallback(e); return
        self.save()
        try: plan=self.tc.punpun_check(e.path)
        except Exception as ex: self.show_fallback(e); return self._tool_error(ex,quiet=True)
        self._run_process(plan.argv,plan.cwd)
    def _tool_error(self,ex,quiet=False):
        self.append_output(f"\n{ex}\n");
        if not quiet and "PunPun" in str(ex): self.toolchain_dialog()
    def toolchain_dialog(self): ToolchainDialog(self.tc,self).exec(); self._restart_lsp()
    def editor_changed(self,e):
        timer=self._checks.get(e)
        if timer is None:
            timer=QTimer(self); timer.setSingleShot(True); timer.timeout.connect(lambda ed=e:self.smart_update(ed)); self._checks[e]=timer
        timer.start(350)
        if kind_for(e.path).id=="punpun" and self.lsp:
            try:self.lsp.change_document(e.path,e.toPlainText(),e.version)
            except Exception: pass
    def smart_update(self,e): self.show_fallback(e); self.refresh_side_info()
    def show_fallback(self,e):
        ps=fallback_problems(e.toPlainText()); self._problems[str(e.path)]=ps; self.render_problems()
    def on_lsp_diagnostics(self,uri,diags):
        from urllib.parse import urlparse,unquote; path=unquote(urlparse(uri).path); rows=[]
        from .smart import Problem
        for d in diags:
            s=d.get("range",{}).get("start",{}); sev={1:"error",2:"warning",3:"info",4:"hint"}.get(d.get("severity"),"info"); rows.append(Problem(sev,int(s.get("line",0))+1,int(s.get("character",0))+1,str(d.get("message","")),str(d.get("code", ""))))
        self._problems[path]=rows; self.render_problems()
    def render_problems(self):
        self.problems.clear()
        for path,rows in self._problems.items():
            for p in rows:
                item=QListWidgetItem(f"{p.severity.upper():7} {Path(path).name}:{p.line}:{p.column}  {p.code} {p.message}"); item.setData(Qt.ItemDataRole.UserRole,(path,p.line,p.column)); self.problems.addItem(item)
    def jump_problem(self,item):
        path,line,col=item.data(Qt.ItemDataRole.UserRole); p=Path(path); self.open_file(p); e=self.current_editor(); cur=e.textCursor(); cur.movePosition(QTextCursor.MoveOperation.Start); cur.movePosition(QTextCursor.MoveOperation.Down,QTextCursor.MoveMode.MoveAnchor,max(0,line-1)); cur.movePosition(QTextCursor.MoveOperation.Right,QTextCursor.MoveMode.MoveAnchor,max(0,col-1)); e.setTextCursor(cur); e.setFocus()
    def refresh_side_info(self,*_):
        e=self.current_editor(); self.symbols.clear();
        if not e:return
        for s in outline(e.toPlainText(),kind_for(e.path).id):
            it=QListWidgetItem(f"{s.name}   {s.kind}"); it.setData(Qt.ItemDataRole.UserRole,s.line); it.setToolTip(s.signature); self.symbols.addItem(it)
        self.statusBar().showMessage(f"{kind_for(e.path).label}  ·  {e.path}")
    def jump_symbol(self,item):
        e=self.current_editor(); line=item.data(Qt.ItemDataRole.UserRole); cur=e.textCursor(); cur.movePosition(QTextCursor.MoveOperation.Start); cur.movePosition(QTextCursor.MoveOperation.Down,QTextCursor.MoveMode.MoveAnchor,line-1); e.setTextCursor(cur); e.setFocus()
    def run_terminal(self):
        cmd=self.termin.text().strip();
        if not cmd:return
        self.termin.clear()
        active=getattr(self,"_interactive_process",None)
        if active is not None and active.state()!=QProcess.ProcessState.NotRunning:
            active.write((cmd+"\n").encode()); return
        self.termout.appendPlainText(f"$ {cmd}"); shell=os.environ.get("COMSPEC","cmd.exe") if os.name=="nt" else os.environ.get("SHELL","/bin/sh"); args=["/c",cmd] if os.name=="nt" else ["-lc",cmd]; p=QProcess(self); p.setWorkingDirectory(str(self.root)); p.readyReadStandardOutput.connect(lambda:self.termout.appendPlainText(bytes(p.readAllStandardOutput()).decode(errors="replace").rstrip())); p.readyReadStandardError.connect(lambda:self.termout.appendPlainText(bytes(p.readAllStandardError()).decode(errors="replace").rstrip())); p.start(shell,args); self._terminal_process=p
    def update_context_help(self,e):
        if kind_for(e.path).id != "punpun": return
        cur=e.textCursor(); cur.select(QTextCursor.SelectionType.WordUnderCursor); word=cur.selectedText(); tip=help_for_word(word)
        if tip: self.statusBar().showMessage(tip)

    def semantic_hover(self):
        e=self.current_editor()
        if not e or kind_for(e.path).id!="punpun": return
        if not self.lsp:
            self.update_context_help(e); return
        c=e.textCursor(); block=c.blockNumber(); col=c.positionInBlock()
        try: result=self.lsp.hover(e.path,block,col)
        except Exception as ex: return self.append_output(f"Hover unavailable: {ex}\n")
        if not result: return
        contents=result.get("contents",result) if isinstance(result,dict) else result
        if isinstance(contents,dict): text=str(contents.get("value",contents))
        elif isinstance(contents,list): text="\n".join(str(x.get("value",x) if isinstance(x,dict) else x) for x in contents)
        else: text=str(contents)
        QToolTip.showText(e.mapToGlobal(e.cursorRect().bottomRight()),text,e)

    def semantic_complete(self):
        e=self.current_editor()
        if not e or kind_for(e.path).id!="punpun" or not self.lsp: return
        c=e.textCursor(); line=c.blockNumber(); col=c.positionInBlock()
        try: result=self.lsp.complete(e.path,line,col) or []
        except Exception as ex: return self.append_output(f"Completion unavailable: {ex}\n")
        items=result.get("items",[]) if isinstance(result,dict) else result
        if not items: return
        menu=QMenu(e)
        for item in items[:80]:
            label=item.get("label","") if isinstance(item,dict) else str(item)
            insert=(item.get("insertText") or label) if isinstance(item,dict) else label
            if not label: continue
            act=menu.addAction(label); act.triggered.connect(lambda checked=False,text=insert,ed=e: ed.insertPlainText(text))
        menu.exec(e.mapToGlobal(e.cursorRect().bottomRight()))

    def _maybe_start_lsp(self):
        ppc=self.tc.ppc();
        if not ppc: return
        def cb(uri,ds): self.bridge.diagnostics.emit(uri,ds)
        try:self.lsp=PunPunLspClient(ppc,cb); self.lsp.start(self.root); self.statusBar().showMessage("PunPun language service connected",3000)
        except Exception as e:self.lsp=None; self.append_output(f"PunPun language service unavailable: {e}\n")
    def _restart_lsp(self):
        if self.lsp:self.lsp.close(); self.lsp=None
        self._maybe_start_lsp()
    def closeEvent(self,event):
        if self.lsp:self.lsp.close()
        super().closeEvent(event)
