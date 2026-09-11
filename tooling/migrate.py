#!/usr/bin/env python3
"""Best-effort PunPun 0.4 -> 0.5 syntax migrator.

The compiler keeps the migration grammar during the 0.5 beta, so this tool is
intentionally conservative: transform syntax we understand, leave unknown text
visible, then let `pp check` point at anything requiring manual attention.
"""
from __future__ import annotations
import re, sys
from pathlib import Path

TYPE_MAP = {"int":"i64", "float":"f64", "str":"String", "bool":"bool", "nums":"nums", "void":"void"}

def ty(value: str) -> str:
    return TYPE_MAP.get(value.strip(), value.strip())


def modern_expr(value: str) -> str:
    # Rewrite legacy operators only outside string literals. This keeps a text
    # value such as "yes and no" byte-for-byte unchanged.
    parts=[]; buf=[]; quote=None; escape=False
    def flush_code():
        code=''.join(buf); buf.clear()
        code=re.sub(r"\byes\b","true",code); code=re.sub(r"\bno\b","false",code)
        code=re.sub(r"\band\b","&&",code); code=re.sub(r"\bor\b","||",code); code=re.sub(r"\bnot\b","!",code)
        code=code.replace("<-", "=")
        parts.append(code)
    i=0
    while i < len(value):
        c=value[i]
        if quote is None:
            if c in ('\"', "'"):
                flush_code(); quote=c; buf.append(c)
            else: buf.append(c)
        else:
            buf.append(c)
            if escape: escape=False
            elif c=='\\': escape=True
            elif c==quote:
                parts.append(''.join(buf)); buf.clear(); quote=None
        i+=1
    if quote is None: flush_code()
    else: parts.append(''.join(buf))
    return ''.join(parts)

def params(raw: str) -> str:
    result=[]
    for part in [p.strip() for p in raw.split(',') if p.strip()]:
        m=re.fullmatch(r"([A-Za-z_]\w*)\s+as\s+([A-Za-z_]\w*)", part)
        result.append(f"{m.group(1)}: {ty(m.group(2))}" if m else part)
    return ', '.join(result)

def migrate(source: str) -> str:
    out=[]; stack=[]
    lines=source.replace('\r\n','\n').replace('\r','\n').splitlines()
    for raw in lines:
        indent=re.match(r"\s*",raw).group(0); text=raw.strip()
        if not text: out.append(''); continue
        if text.startswith('//') or text.startswith('#'): out.append(raw); continue
        m=re.fullmatch(r"bring\s+([A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*)\s*;?",text)
        if m: out.append(indent+"bring "+m.group(1).replace('.', '::')+";"); continue
        if text == 'launch:': out.append(indent+'launch {'); stack.append('block'); continue
        m=re.fullmatch(r"craft\s+([A-Za-z_]\w*)\((.*)\)(?:\s+gives\s+([A-Za-z_]\w*))?:",text)
        if m:
            result=f" -> {ty(m.group(3))}" if m.group(3) else ''
            out.append(indent+f"fn {m.group(1)}({params(m.group(2))}){result} {{"); stack.append('block'); continue
        m=re.fullmatch(r"shape\s+([A-Za-z_]\w*):",text)
        if m: out.append(indent+f"struct {m.group(1)} {{"); stack.append('shape'); continue
        if text == 'done':
            if stack: stack.pop()
            out.append(indent+'}'); continue
        m=re.fullmatch(r"otherwise:",text)
        if m:
            out.append(indent+'} else {'); continue
        m=re.fullmatch(r"when\s+(.+):",text)
        if m: out.append(indent+f"if {modern_expr(m.group(1))} {{"); stack.append('block'); continue
        m=re.fullmatch(r"whilst\s+(.+):",text)
        if m: out.append(indent+f"while {modern_expr(m.group(1))} {{"); stack.append('block'); continue
        m=re.fullmatch(r"each\s+([A-Za-z_]\w*)\s+from\s+(.+)\s+until\s+(.+):",text)
        if m: out.append(indent+f"for {m.group(1)} in {modern_expr(m.group(2))}..{modern_expr(m.group(3))} {{"); stack.append('block'); continue
        if stack and stack[-1]=='shape':
            m=re.fullmatch(r"([A-Za-z_]\w*)\s+as\s+([A-Za-z_]\w*)\s*;?",text)
            if m: out.append(indent+f"{m.group(1)}: {ty(m.group(2))},"); continue
        # A binding, with or without an explicit type. The annotated form was
        # missed before, which left `keep name as Type = value` half-converted.
        m=re.fullmatch(r"(pin|keep)\s+([A-Za-z_]\w*)"
                       r"(?:\s+as\s+([A-Za-z_]\w*(?:<[^>]*>)?))?"
                       r"\s*(?:<-|=)\s*(.+?)\s*;?",text)
        if m:
            # Legacy `keep` is mutable; legacy `pin` is immutable.
            mut=' mut' if m.group(1)=='keep' else ''
            annotation=f": {ty(m.group(3))}" if m.group(3) else ''
            expr=modern_expr(m.group(4).rstrip(';'))
            out.append(indent+f"let{mut} {m.group(2)}{annotation} = {expr};"); continue
        m=re.fullmatch(r"say\s+(.+)\s*;?",text)
        if m: out.append(indent+f"say({modern_expr(m.group(1).rstrip(';'))});"); continue
        m=re.fullmatch(r"give(?:\s+(.+))?\s*;?",text)
        if m: out.append(indent+(f"return {modern_expr(m.group(1).rstrip(';'))};" if m.group(1) else 'return;')); continue
        if text == 'leave': out.append(indent+'break;'); continue
        if text == 'next': out.append(indent+'continue;'); continue
        # Translate the old word operators/literals without touching strings.
        code=modern_expr(text)
        if not code.endswith((';','{','}')): code+=';'
        out.append(indent+code)
    return '\n'.join(out).rstrip()+'\n'

def main() -> int:
    if len(sys.argv)!=2:
        print('usage: migrate.py file.pp',file=sys.stderr); return 2
    path=Path(sys.argv[1]); source=path.read_text(encoding='utf-8')
    backup=path.with_suffix(path.suffix+'.legacy')
    if not backup.exists(): backup.write_text(source,encoding='utf-8')
    path.write_text(migrate(source),encoding='utf-8')
    print(f"migrated {path} (backup: {backup})")
    return 0
if __name__=='__main__': raise SystemExit(main())
