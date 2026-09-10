MOCHA = {
    "base":"#1e1e2e","mantle":"#181825","crust":"#11111b","surface0":"#313244",
    "surface1":"#45475a","surface2":"#585b70","text":"#cdd6f4","subtext0":"#a6adc8",
    "blue":"#89b4fa","green":"#a6e3a1","red":"#f38ba8","yellow":"#f9e2af",
    "mauve":"#cba6f7","peach":"#fab387","teal":"#94e2d5",
}

def stylesheet():
    c=MOCHA
    return f"""
    QMainWindow, QWidget {{ background: {c['base']}; color: {c['text']}; }}
    QMenuBar, QMenu, QToolBar {{ background: {c['mantle']}; color: {c['text']}; border: 0; }}
    QToolBar {{ spacing: 5px; padding: 5px; border-bottom: 1px solid {c['surface0']}; }}
    QToolButton {{ border: 0; border-radius: 6px; padding: 6px 9px; }}
    QToolButton:hover {{ background: {c['surface0']}; }}
    QTreeView, QListWidget, QPlainTextEdit, QTextBrowser, QLineEdit {{ background: {c['mantle']}; color: {c['text']}; border: 0; selection-background-color: {c['surface1']}; }}
    QTabWidget::pane {{ border: 0; }}
    QTabBar::tab {{ background: {c['mantle']}; color: {c['subtext0']}; padding: 8px 14px; border-right: 1px solid {c['surface0']}; }}
    QTabBar::tab:selected {{ background: {c['base']}; color: {c['text']}; border-top: 2px solid {c['blue']}; }}
    QDockWidget::title {{ background: {c['mantle']}; padding: 6px; }}
    QStatusBar {{ background: {c['crust']}; color: {c['subtext0']}; }}
    QPushButton {{ background: {c['surface0']}; color: {c['text']}; border: 0; border-radius: 7px; padding: 7px 12px; }}
    QPushButton:hover {{ background: {c['surface1']}; }}
    QProgressBar {{ border: 0; background: {c['surface0']}; border-radius: 4px; text-align:center; }}
    QProgressBar::chunk {{ background: {c['blue']}; border-radius:4px; }}
    """
