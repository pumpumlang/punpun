from __future__ import annotations
import re
from pathlib import Path
from PySide6.QtCore import Qt, QRect, QSize, Signal
from PySide6.QtGui import QColor, QFont, QPainter, QSyntaxHighlighter, QTextCharFormat
from PySide6.QtWidgets import QPlainTextEdit, QWidget
from .filetypes import kind_for
from .theme import MOCHA

class LineNumberArea(QWidget):
    def __init__(self, editor): super().__init__(editor); self.editor=editor
    def sizeHint(self): return QSize(self.editor.line_number_width(),0)
    def paintEvent(self,event): self.editor.paint_line_numbers(event)

class SyntaxHighlighter(QSyntaxHighlighter):
    def __init__(self, document, language):
        super().__init__(document); self.language=language; self.rules=[]; self._build()
    def fmt(self,color,bold=False):
        f=QTextCharFormat(); f.setForeground(QColor(color));
        if bold: f.setFontWeight(QFont.Weight.DemiBold)
        return f
    def _build(self):
        c=MOCHA; L=self.language
        if L=="punpun":
            self.rules += [(r'\b(launch|bring|import|fn|let|const|mut|return|if|else|match|case|while|for|in|break|continue|async|await|unsafe|raw|extern|native|object|struct|contract|enum|sealed|public|private|protected|static)\b',self.fmt(c['mauve'],True)),(r'\b(i64|i32|u64|u32|f64|f32|bool|String|void|int|float|str|Option|Result|List)\b',self.fmt(c['yellow'])),(r'\b(true|false)\b',self.fmt(c['peach'])),(r'\b(say|print|println|len|panic|assert|sleep_ms)\b(?=\s*\()',self.fmt(c['blue']))]
        elif L in {"c","cpp","header"}:
            self.rules += [(r'\b(auto|bool|break|case|char|class|const|constexpr|continue|default|do|double|else|enum|extern|float|for|if|inline|int|long|namespace|private|protected|public|return|short|signed|sizeof|static|struct|switch|template|this|typedef|typename|union|unsigned|using|virtual|void|volatile|while)\b',self.fmt(c['mauve'],True)),(r'^\s*#\s*\w+.*$',self.fmt(c['peach']))]
        elif L=="markdown":
            self.rules += [(r'^#{1,6}\s+.*$',self.fmt(c['blue'],True)),(r'\*\*[^*]+\*\*',self.fmt(c['yellow'],True)),(r'`[^`]+`',self.fmt(c['green']))]
        self.rules += [(r'"(?:\\.|[^"\\])*"',self.fmt(c['green'])),(r"'(?:\\.|[^'\\])*'",self.fmt(c['green'])),(r'\b\d+(?:\.\d+)?\b',self.fmt(c['peach'])),(r'//.*$|#(?!include|define|ifdef|ifndef|endif).*$',self.fmt(c['surface2']))]
    def highlightBlock(self,text):
        for pat,fmt in self.rules:
            for m in re.finditer(pat,text): self.setFormat(m.start(),m.end()-m.start(),fmt)

class CodeEditor(QPlainTextEdit):
    changed=Signal(object)
    def __init__(self,path: Path):
        super().__init__(); self.path=path; self.language=kind_for(path).id; self.version=1
        font=QFont("JetBrains Mono"); font.setStyleHint(QFont.StyleHint.Monospace); font.setPointSize(11); self.setFont(font)
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap); self.setTabStopDistance(self.fontMetrics().horizontalAdvance(' ')*4)
        self.line_area=LineNumberArea(self); self.blockCountChanged.connect(self.update_margins); self.updateRequest.connect(self.update_area); self.cursorPositionChanged.connect(self.highlight_line); self.textChanged.connect(self._changed)
        self.update_margins(); self.highlight_line(); self.highlighter=SyntaxHighlighter(self.document(),self.language)
    def _changed(self): self.version+=1; self.changed.emit(self)
    def line_number_width(self): return 12+self.fontMetrics().horizontalAdvance('9')*max(2,len(str(self.blockCount())))
    def update_margins(self,*_): self.setViewportMargins(self.line_number_width(),0,0,0)
    def resizeEvent(self,e): super().resizeEvent(e); cr=self.contentsRect(); self.line_area.setGeometry(QRect(cr.left(),cr.top(),self.line_number_width(),cr.height()))
    def update_area(self,rect,dy):
        if dy: self.line_area.scroll(0,dy)
        else: self.line_area.update(0,rect.y(),self.line_area.width(),rect.height())
    def paint_line_numbers(self,event):
        p=QPainter(self.line_area); p.fillRect(event.rect(),QColor(MOCHA['mantle'])); block=self.firstVisibleBlock(); num=block.blockNumber(); top=round(self.blockBoundingGeometry(block).translated(self.contentOffset()).top()); bottom=top+round(self.blockBoundingRect(block).height())
        while block.isValid() and top<=event.rect().bottom():
            if block.isVisible() and bottom>=event.rect().top(): p.setPen(QColor(MOCHA['surface2'])); p.drawText(0,top,self.line_area.width()-7,self.fontMetrics().height(),Qt.AlignmentFlag.AlignRight,str(num+1))
            block=block.next(); top=bottom; bottom=top+round(self.blockBoundingRect(block).height()); num+=1
    def highlight_line(self):
        s=QPlainTextEdit.ExtraSelection(); s.format.setBackground(QColor(MOCHA['surface0'])); s.format.setProperty(QTextCharFormat.Property.FullWidthSelection,True); s.cursor=self.textCursor(); s.cursor.clearSelection(); self.setExtraSelections([s])
