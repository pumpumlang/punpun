from __future__ import annotations
from dataclasses import dataclass
import json, re

@dataclass(frozen=True)
class Problem:
    severity: str
    line: int
    column: int
    message: str
    code: str = ""

@dataclass(frozen=True)
class Symbol:
    name: str
    kind: str
    line: int
    signature: str

PUNPUN_HELP={
 "fn":"Declare a function: fn name(arg: Type) -> Type { ... }",
 "launch":"Program entry block: launch { ... }",
 "bring":"Import a module: bring std::io;",
 "let":"Declare a local binding. Add mut when the binding must change.",
 "match":"Pattern-match over values and enum variants.",
 "say":"Print a value through PunPun's standard output support.",
 "async":"Declare asynchronous work. The C backend has full async support.",
 "await":"Wait for an asynchronous result.",
}

def fallback_problems(text: str) -> list[Problem]:
    problems=[]; stack=[]; pairs={')':'(',']':'[','}':'{'}
    in_string=False; esc=False
    for lineno,line in enumerate(text.splitlines(),1):
        if line.rstrip()!=line: problems.append(Problem("hint",lineno,len(line.rstrip())+1,"Trailing whitespace"))
        for col,ch in enumerate(line,1):
            if in_string:
                if esc: esc=False
                elif ch=='\\': esc=True
                elif ch=='"': in_string=False
                continue
            if ch=='"': in_string=True; continue
            if ch in '([{': stack.append((ch,lineno,col))
            elif ch in pairs:
                if not stack or stack[-1][0]!=pairs[ch]: problems.append(Problem("warning",lineno,col,f"Unmatched '{ch}'"))
                else: stack.pop()
    for ch,l,c in stack[-20:]: problems.append(Problem("warning",l,c,f"Unclosed '{ch}'"))
    return problems

def parse_ppc_json(output: str) -> list[Problem]:
    out=[]; values=[]
    try: values=[json.loads(output)]
    except Exception:
        for line in output.splitlines():
            try: values.append(json.loads(line))
            except Exception: pass
    def walk(v):
        if isinstance(v,list):
            for x in v: walk(x)
        elif isinstance(v,dict):
            if any(k in v for k in ("message","diagnostic")):
                msg=str(v.get("message") or v.get("diagnostic") or "Compiler diagnostic")
                sev=str(v.get("severity") or v.get("level") or "error").lower(); code=str(v.get("code") or "")
                span=v.get("span") or v.get("range") or {}; start=span.get("start",span) if isinstance(span,dict) else {}
                line=int(start.get("line",v.get("line",1)) or 1); col=int(start.get("column",start.get("character",v.get("column",1))) or 1)
                out.append(Problem(sev,max(1,line),max(1,col),msg,code))
            else:
                for x in v.values():
                    if isinstance(x,(dict,list)): walk(x)
    for v in values: walk(v)
    return out

def outline(text: str, language: str) -> list[Symbol]:
    items=[]
    if language=="punpun":
        patterns=[("function",re.compile(r'^\s*(?:extern\s+native\s+)?fn\s+([A-Za-z_]\w*)\s*(\([^\n{]*\)(?:\s*->\s*[^\n{]+)?)')),("type",re.compile(r'^\s*(?:sealed\s+)?(?:object|struct|contract|enum)\s+([A-Za-z_]\w*)'))]
    elif language in {"c","cpp","header"}:
        patterns=[("function",re.compile(r'^\s*(?!if\b|for\b|while\b|switch\b)(?:[\w:<>&*~]+\s+)+([A-Za-z_]\w*)\s*(\([^;{}]*\))\s*(?:const\s*)?(?:\{|$)'))]
    elif language=="markdown": patterns=[("heading",re.compile(r'^(#{1,6})\s+(.+)$'))]
    else: patterns=[]
    for i,line in enumerate(text.splitlines(),1):
        for kind,rx in patterns:
            m=rx.search(line)
            if not m: continue
            name=m.group(2) if language=="markdown" else m.group(1)
            items.append(Symbol(name,kind,i,line.strip())); break
    return items

def help_for_word(word: str) -> str | None:
    return PUNPUN_HELP.get(word)
