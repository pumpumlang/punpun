#!/usr/bin/env python3
"""LSP server regression test.

Drives `ppc serve --stdio` over the real protocol and checks the responses.
Separate from the language regression suite because it exercises a different
surface: protocol framing, JSON encoding, and the semantic query layer, rather
than code generation.

Run directly, or via `make test-lsp`.
"""
import json, os, subprocess, sys, tempfile

COMPILER_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROOT = os.path.dirname(COMPILER_ROOT)
PPC = os.environ.get("PPC", os.path.join(ROOT, "build", "ppc"))

SOURCE = '''fn double(value: int) -> int {
    return value * 2;
}

struct Point { x: int, y: int }

enum State { Ready, Busy }

launch {
    let answer = double(21);
    say(answer);
    let broken: int = "text";
}
'''


def frame(obj):
    body = json.dumps(obj)
    return f"Content-Length: {len(body)}\r\n\r\n{body}".encode()


def unframe(data):
    messages = []
    while b"Content-Length:" in data:
        start = data.index(b"Content-Length:")
        end = data.index(b"\r\n\r\n", start)
        length = int(data[start + 15:end])
        messages.append(json.loads(data[end + 4:end + 4 + length]))
        data = data[end + 4 + length:]
    return messages


def run_session(path, requests):
    uri = "file://" + path
    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize",
         "params": {"rootPath": os.path.dirname(path), "capabilities": {}}},
        {"jsonrpc": "2.0", "method": "initialized", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": uri, "languageId": "punpun",
                                     "version": 1, "text": SOURCE}}},
    ]
    messages.extend(requests(uri))
    messages.append({"jsonrpc": "2.0", "id": 999, "method": "shutdown", "params": {}})
    messages.append({"jsonrpc": "2.0", "method": "exit", "params": {}})

    payload = b"".join(frame(m) for m in messages)
    proc = subprocess.run([PPC, "serve", "--stdio"], input=payload,
                          capture_output=True, timeout=120)
    return proc, unframe(proc.stdout)


def main():
    if not os.path.exists(PPC):
        print("ppc is not built; run make first")
        return 2

    failures = []

    def check(name, condition, detail=""):
        if condition:
            print(f"  pass  {name}")
        else:
            print(f"  FAIL  {name}   {detail}")
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="ppclsp-") as scratch:
        path = os.path.join(scratch, "demo.pp")
        open(path, "w").write(SOURCE)

        def requests(uri):
            return [
                {"jsonrpc": "2.0", "id": 2, "method": "textDocument/documentSymbol",
                 "params": {"textDocument": {"uri": uri}}},
                {"jsonrpc": "2.0", "id": 3, "method": "textDocument/hover",
                 "params": {"textDocument": {"uri": uri},
                            "position": {"line": 0, "character": 4}}},
                {"jsonrpc": "2.0", "id": 4, "method": "textDocument/completion",
                 "params": {"textDocument": {"uri": uri},
                            "position": {"line": 9, "character": 4}}},
                {"jsonrpc": "2.0", "id": 5, "method": "textDocument/nonsense",
                 "params": {}},
            ]

        proc, out = run_session(path, requests)
        by_id = {m["id"]: m for m in out if "id" in m}

        check("clean exit status", proc.returncode == 0,
              f"got {proc.returncode}")

        # initialize advertises only implemented capabilities.
        caps = by_id.get(1, {}).get("result", {}).get("capabilities", {})
        for capability in ("hoverProvider", "definitionProvider",
                           "documentSymbolProvider", "completionProvider"):
            check(f"advertises {capability}", capability in caps)

        # Diagnostics come from the real compiler, so the deliberate type error
        # must appear with its real code at its real location.
        published = [m for m in out
                     if m.get("method") == "textDocument/publishDiagnostics"]
        check("publishes diagnostics", len(published) >= 1)
        if published:
            items = published[0]["params"]["diagnostics"]
            check("reports the type error", any(d["code"] == "E0400" for d in items),
                  f"codes: {[d['code'] for d in items]}")
            check("reports it on the right line",
                  any(d["range"]["start"]["line"] == 11 for d in items),
                  f"lines: {[d['range']['start']['line'] for d in items]}")

        symbols = by_id.get(2, {}).get("result", [])
        names = {s["name"] for s in symbols}
        check("finds every top-level declaration",
              {"double", "main", "Point", "State"} <= names, f"got {names}")
        point = next((s for s in symbols if s["name"] == "Point"), None)
        check("nests struct fields",
              point is not None and {c["name"] for c in point.get("children", [])} == {"x", "y"})
        state = next((s for s in symbols if s["name"] == "State"), None)
        check("nests enum variants",
              state is not None and {c["name"] for c in state.get("children", [])} == {"Ready", "Busy"})

        hover = by_id.get(3, {}).get("result") or {}
        text = hover.get("contents", {}).get("value", "")
        check("hover shows the full signature",
              "fn double(value: int) -> int" in text, f"got {text!r}")

        completions = by_id.get(4, {}).get("result", [])
        labels = {c["label"] for c in completions}
        check("completes program declarations", {"Point", "double"} <= labels)
        check("completes builtins", "println" in labels)
        check("qualifies enum variants", "State::Ready" in labels)
        check("does not offer bare variant names", "Ready" not in labels)

        # An unknown method must produce a JSON-RPC error, not a crash.
        check("rejects unknown methods",
              by_id.get(5, {}).get("error", {}).get("code") == -32601)

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s): {', '.join(failures)}")
        return 1
    print("ok: all LSP checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
