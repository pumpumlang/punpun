from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path

@dataclass(frozen=True)
class FileKind:
    id: str
    label: str
    runnable: bool = False
    debuggable: bool = False

PUNPUN = FileKind("punpun", "PunPun", True, True)
C = FileKind("c", "C", True, True)
CPP = FileKind("cpp", "C++", True, True)
HEADER = FileKind("header", "C/C++ Header")
MARKDOWN = FileKind("markdown", "Markdown")
TEXT = FileKind("text", "Text")

_CPP = {".cc", ".cpp", ".cxx", ".c++"}
_HEADER = {".h", ".hh", ".hpp", ".hxx", ".inl"}
_MD = {".md", ".markdown", ".mdown"}

def kind_for(path: str | Path) -> FileKind:
    suffix = Path(path).suffix.lower()
    if suffix == ".pp": return PUNPUN
    if suffix == ".c": return C
    if suffix in _CPP: return CPP
    if suffix in _HEADER: return HEADER
    if suffix in _MD: return MARKDOWN
    return TEXT
