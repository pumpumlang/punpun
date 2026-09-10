from __future__ import annotations
import json, queue, subprocess, threading
from pathlib import Path
from typing import Callable

class LspError(RuntimeError): pass

class PunPunLspClient:
    """Small JSON-RPC/LSP client for `ppc serve --stdio`.

    It deliberately contains no PunPun semantic rules. PPC remains the source of truth.
    """
    def __init__(self, ppc: str, diagnostics: Callable[[str,list],None] | None=None):
        self.ppc=ppc; self.proc=None; self._id=0; self._pending={}; self._lock=threading.Lock(); self._diag=diagnostics

    def start(self, root: Path):
        if self.proc and self.proc.poll() is None: return
        self.proc=subprocess.Popen([self.ppc,"serve","--stdio"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        threading.Thread(target=self._reader,daemon=True).start()
        result=self.request("initialize",{"processId":None,"rootUri":root.resolve().as_uri(),"capabilities":{}},timeout=5)
        self.notify("initialized",{})
        return result

    def close(self):
        if not self.proc: return
        try: self.request("shutdown",None,timeout=1); self.notify("exit",None)
        except Exception: pass
        try: self.proc.terminate()
        except Exception: pass
        self.proc=None

    def _send(self,obj):
        if not self.proc or not self.proc.stdin: raise LspError("LSP is not running")
        raw=json.dumps(obj,separators=(",",":")).encode(); frame=f"Content-Length: {len(raw)}\r\n\r\n".encode()+raw
        with self._lock: self.proc.stdin.write(frame); self.proc.stdin.flush()

    def notify(self,method,params): self._send({"jsonrpc":"2.0","method":method,"params":params})

    def request(self,method,params,timeout=2):
        self._id+=1; ident=self._id; q=queue.Queue(maxsize=1); self._pending[ident]=q
        self._send({"jsonrpc":"2.0","id":ident,"method":method,"params":params})
        try: msg=q.get(timeout=timeout)
        except queue.Empty: self._pending.pop(ident,None); raise LspError(f"LSP request timed out: {method}")
        if "error" in msg: raise LspError(str(msg["error"]))
        return msg.get("result")

    def _reader(self):
        s=self.proc.stdout if self.proc else None
        while s:
            headers={}
            while True:
                line=s.readline()
                if not line: return
                if line in (b"\r\n",b"\n"): break
                k,_,v=line.decode(errors="replace").partition(":"); headers[k.lower().strip()]=v.strip()
            n=int(headers.get("content-length",0)); data=s.read(n)
            if not data: return
            try: msg=json.loads(data)
            except Exception: continue
            if "id" in msg and msg.get("id") in self._pending:
                self._pending.pop(msg["id"]).put(msg); continue
            if msg.get("method")=="textDocument/publishDiagnostics" and self._diag:
                p=msg.get("params",{}); self._diag(p.get("uri",""),p.get("diagnostics",[]))

    @staticmethod
    def uri(path: Path): return path.resolve().as_uri()
    def open_document(self,path: Path,text: str,version=1):
        self.notify("textDocument/didOpen",{"textDocument":{"uri":self.uri(path),"languageId":"punpun","version":version,"text":text}})
    def change_document(self,path: Path,text: str,version: int):
        self.notify("textDocument/didChange",{"textDocument":{"uri":self.uri(path),"version":version},"contentChanges":[{"text":text}]})
    def close_document(self,path: Path): self.notify("textDocument/didClose",{"textDocument":{"uri":self.uri(path)}})
    def hover(self,path: Path,line: int,char: int):
        return self.request("textDocument/hover",{"textDocument":{"uri":self.uri(path)},"position":{"line":line,"character":char}})
    def complete(self,path: Path,line: int,char: int):
        return self.request("textDocument/completion",{"textDocument":{"uri":self.uri(path)},"position":{"line":line,"character":char}})
    def symbols(self,path: Path):
        return self.request("textDocument/documentSymbol",{"textDocument":{"uri":self.uri(path)}})
