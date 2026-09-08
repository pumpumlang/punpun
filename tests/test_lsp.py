#!/usr/bin/env python3
import json
import os
import select
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path
from urllib.parse import quote

ROOT = Path(__file__).resolve().parent.parent
SERVER = ROOT / "tooling" / "lsp" / "server.js"
PPC = ROOT / "build" / "ppc"


def file_uri(path):
    return "file://" + quote(str(Path(path).resolve()))


class LspProcess:
    def __init__(self, cwd):
        env = os.environ.copy()
        env["PUNPUN_PPC"] = str(PPC)
        self.process = subprocess.Popen(
            ["node", str(SERVER)], cwd=cwd, env=env,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        self.next_id = 1
        self.notifications = []

    def close(self):
        if self.process.poll() is None:
            try:
                self.request("shutdown", None)
                self.notify("exit", None)
                self.process.wait(timeout=3)
            except Exception:
                self.process.kill()
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            if stream:
                try:
                    stream.close()
                except Exception:
                    pass

    def send(self, message):
        data = json.dumps(message, separators=(",", ":")).encode("utf-8")
        self.process.stdin.write(f"Content-Length: {len(data)}\r\n\r\n".encode("ascii") + data)
        self.process.stdin.flush()

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def request(self, method, params, timeout=5):
        request_id = self.next_id
        self.next_id += 1
        self.send({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            message = self.read_message(deadline - time.monotonic())
            if message is None:
                continue
            if message.get("id") == request_id:
                if "error" in message:
                    raise AssertionError(message["error"])
                return message.get("result")
            self.notifications.append(message)
        raise AssertionError(f"timed out waiting for LSP response to {method}")

    def read_message(self, timeout=5):
        if timeout <= 0:
            return None
        ready, _, _ = select.select([self.process.stdout], [], [], timeout)
        if not ready:
            return None
        headers = {}
        while True:
            line = self.process.stdout.readline()
            if not line:
                stderr = self.process.stderr.read().decode("utf-8", errors="replace")
                raise AssertionError(f"LSP exited unexpectedly: {stderr}")
            if line in (b"\r\n", b"\n"):
                break
            key, value = line.decode("ascii").split(":", 1)
            headers[key.lower()] = value.strip()
        length = int(headers["content-length"])
        body = self.process.stdout.read(length)
        return json.loads(body.decode("utf-8"))

    def wait_notification(self, method, timeout=5):
        deadline = time.monotonic() + timeout
        for index, message in enumerate(self.notifications):
            if message.get("method") == method:
                return self.notifications.pop(index)
        while time.monotonic() < deadline:
            message = self.read_message(deadline - time.monotonic())
            if message and message.get("method") == method:
                return message
            if message:
                self.notifications.append(message)
        raise AssertionError(f"timed out waiting for notification {method}")


@unittest.skipUnless(shutil.which("node"), "Node.js is required for LSP tests")
class LspTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp(prefix="punpun-lsp-test-")
        self.addCleanup(shutil.rmtree, self.directory, ignore_errors=True)
        self.client = LspProcess(self.directory)
        self.addCleanup(self.client.close)
        self.uri = file_uri(Path(self.directory) / "main.pp")
        self.source = (
            "fn greet(name: String) -> String {\n"
            "    return concat(\"hi \", name);\n"
            "}\n"
            "fn main() {\n"
            "    println(greet(\"PunPun\"));\n"
            "}\n"
        )
        Path(self.directory, "main.pp").write_text(self.source, encoding="utf-8")
        initialized = self.client.request("initialize", {
            "processId": os.getpid(), "rootUri": file_uri(self.directory), "capabilities": {},
        })
        self.assertEqual(initialized["serverInfo"]["name"], "punpun-lsp")
        self.assertTrue(initialized["capabilities"]["definitionProvider"])
        self.assertIn("semanticTokensProvider", initialized["capabilities"])
        self.client.notify("initialized", {})
        self.client.notify("textDocument/didOpen", {
            "textDocument": {"uri": self.uri, "languageId": "punpun", "version": 1, "text": self.source}
        })
        # Consume initial diagnostics so tests don't race with didOpen.
        opened = self.client.wait_notification("textDocument/publishDiagnostics")
        self.assertEqual(opened["params"]["diagnostics"], [])

    def test_completion_definition_signature_and_live_diagnostics(self):
        completions = self.client.request("textDocument/completion", {
            "textDocument": {"uri": self.uri}, "position": {"line": 4, "character": 8}
        })
        labels = {item["label"] for item in completions}
        self.assertIn("file_exists", labels)
        self.assertIn("fs_exists", labels)
        self.assertIn("greet", labels)

        definition = self.client.request("textDocument/definition", {
            "textDocument": {"uri": self.uri}, "position": {"line": 4, "character": 16}
        })
        self.assertEqual(definition["uri"], self.uri)
        self.assertEqual(definition["range"]["start"]["line"], 0)

        signature = self.client.request("textDocument/signatureHelp", {
            "textDocument": {"uri": self.uri}, "position": {"line": 4, "character": 23}
        })
        self.assertIn("greet(name: String)", signature["signatures"][0]["label"])

        # Permanent regression for the screenshot failure: unsaved nonsense must
        # be diagnosed by the same semantic engine as pp check.
        invalid = (
            "bring std::stats;\n"
            "launch {\n"
            "    say(\"hello from testproj\");\n"
            "    sdds;\n"
            "}\n"
        )
        self.client.notify("textDocument/didChange", {
            "textDocument": {"uri": self.uri, "version": 2}, "contentChanges": [{"text": invalid}],
        })
        notification = self.client.wait_notification("textDocument/publishDiagnostics")
        diagnostics = notification["params"]["diagnostics"]
        self.assertEqual(len(diagnostics), 1)
        diagnostic = diagnostics[0]
        self.assertEqual(diagnostic["source"], "punpun")
        self.assertEqual(diagnostic["code"], "E0201")
        self.assertIn("unknown name 'sdds'", diagnostic["message"])
        self.assertEqual(diagnostic["range"]["start"], {"line": 3, "character": 4})
        self.assertEqual(diagnostic["range"]["end"], {"line": 3, "character": 8})

    def test_object_member_completion_hover_and_semantic_tokens(self):
        source = (
            "object Player {\n"
            "    private let mut health: i64;\n"
            "    public let name: String;\n"
            "    public init(name: String, health: i64) {\n"
            "        self.name = name;\n"
            "        self.health = health;\n"
            "    }\n"
            "    public fn damage(amount: i64) {\n"
            "        self.health -= amount;\n"
            "    }\n"
            "}\n"
            "fn main() {\n"
            "    let mut player = Player(\"P\", 100);\n"
            "    player.\n"
            "}\n"
        )
        self.client.notify("textDocument/didChange", {
            "textDocument": {"uri": self.uri, "version": 3}, "contentChanges": [{"text": source}],
        })
        completions = self.client.request("textDocument/completion", {
            "textDocument": {"uri": self.uri}, "position": {"line": 13, "character": 11}
        })
        labels = {item["label"] for item in completions}
        self.assertIn("name", labels)
        self.assertIn("damage", labels)
        self.assertNotIn("health", labels)  # private outside Player

        tokens = self.client.request("textDocument/semanticTokens/full", {"textDocument": {"uri": self.uri}})
        self.assertGreater(len(tokens["data"]), 10)

    def test_formatting_is_deterministic(self):
        ugly = "fn main(){\nprintln(\"x\");\n}\n"
        self.client.notify("textDocument/didChange", {
            "textDocument": {"uri": self.uri, "version": 4}, "contentChanges": [{"text": ugly}],
        })
        edits = self.client.request("textDocument/formatting", {
            "textDocument": {"uri": self.uri}, "options": {"tabSize": 4, "insertSpaces": True}
        })
        self.assertEqual(len(edits), 1)
        self.assertEqual(edits[0]["newText"], "fn main(){\n    println(\"x\");\n}\n")

    def test_async_keywords_symbols_and_diagnostics(self):
        source = (
            "async fn fetch_value(value: i64) -> i64 {\n"
            "    return value;\n"
            "}\n"
            "launch {\n"
            "    let task = fetch_value(42);\n"
            "    say(await task);\n"
            "}\n"
        )
        self.client.notify("textDocument/didChange", {
            "textDocument": {"uri": self.uri, "version": 7}, "contentChanges": [{"text": source}],
        })
        notification = self.client.wait_notification("textDocument/publishDiagnostics")
        self.assertEqual(notification["params"]["diagnostics"], [])

        completions = self.client.request("textDocument/completion", {
            "textDocument": {"uri": self.uri}, "position": {"line": 5, "character": 8}
        })
        labels = {item["label"] for item in completions}
        self.assertIn("async", labels)
        self.assertIn("await", labels)
        self.assertIn("fetch_value", labels)

        hover = self.client.request("textDocument/hover", {
            "textDocument": {"uri": self.uri}, "position": {"line": 4, "character": 20}
        })
        self.assertIn("async fetch_value(value: i64) -> i64", hover["contents"]["value"])

        tokens = self.client.request("textDocument/semanticTokens/full", {"textDocument": {"uri": self.uri}})
        self.assertGreater(len(tokens["data"]), 10)

        bad = (
            "async fn fetch_value(value: i64) -> i64 { return value; }\n"
            "fn helper() {\n"
            "    let task = fetch_value(1);\n"
            "    say(await task);\n"
            "}\n"
            "fn main() { helper(); }\n"
        )
        self.client.notify("textDocument/didChange", {
            "textDocument": {"uri": self.uri, "version": 8}, "contentChanges": [{"text": bad}],
        })
        notification = self.client.wait_notification("textDocument/publishDiagnostics")
        diagnostics = notification["params"]["diagnostics"]
        self.assertTrue(any(d.get("code") == "E1503" for d in diagnostics))

    def test_formatter_preserves_injected_source_and_typo_quickfix(self):
        injected = (
            '@inject->c("""\n'
            'int weird(int x) {\n'
            '        return x + 1;\n'
            '}\n'
            '""");\n'
            'launch {\n'
            'say("x");\n'
            '}\n'
        )
        self.client.notify("textDocument/didChange", {
            "textDocument": {"uri": self.uri, "version": 5}, "contentChanges": [{"text": injected}],
        })
        edits = self.client.request("textDocument/formatting", {
            "textDocument": {"uri": self.uri}, "options": {"tabSize": 4, "insertSpaces": True}
        })
        formatted = edits[0]["newText"]
        self.assertIn('        return x + 1;', formatted)
        self.assertIn('launch {\n    say("x");\n}', formatted)

        typo = (
            'launch {\n'
            '    let player = 1;\n'
            '    say(playre);\n'
            '}\n'
        )
        self.client.notify("textDocument/didChange", {
            "textDocument": {"uri": self.uri, "version": 6}, "contentChanges": [{"text": typo}],
        })
        notification = self.client.wait_notification("textDocument/publishDiagnostics")
        diagnostics = notification["params"]["diagnostics"]
        target = next(d for d in diagnostics if d.get("code") == "E0201")
        self.assertIn("did you mean `player`?", target["message"])
        actions = self.client.request("textDocument/codeAction", {
            "textDocument": {"uri": self.uri},
            "range": target["range"],
            "context": {"diagnostics": [target]},
        })
        self.assertTrue(any(a["title"] == "Change to 'player'" for a in actions))

    def test_rename_ignores_comments_and_strings(self):
        source = (
            'fn greet() { println("greet stays text"); }\n'
            '// greet stays in this comment\n'
            'fn main() { greet(); }\n'
        )
        self.client.notify("textDocument/didChange", {
            "textDocument": {"uri": self.uri, "version": 9}, "contentChanges": [{"text": source}],
        })
        self.client.wait_notification("textDocument/publishDiagnostics")
        prepared = self.client.request("textDocument/prepareRename", {
            "textDocument": {"uri": self.uri}, "position": {"line": 0, "character": 5},
        })
        self.assertEqual(prepared["placeholder"], "greet")
        edit = self.client.request("textDocument/rename", {
            "textDocument": {"uri": self.uri}, "position": {"line": 0, "character": 5}, "newName": "welcome",
        })
        self.assertEqual(len(edit["changes"][self.uri]), 2)

    def test_unknown_stdlib_symbol_offers_import(self):
        source = 'launch { let values = [1, 2]; say(stats_sum(values)); }\n'
        self.client.notify("textDocument/didChange", {
            "textDocument": {"uri": self.uri, "version": 10}, "contentChanges": [{"text": source}],
        })
        notification = self.client.wait_notification("textDocument/publishDiagnostics")
        target = next(d for d in notification["params"]["diagnostics"] if d.get("code") == "E0201")
        actions = self.client.request("textDocument/codeAction", {
            "textDocument": {"uri": self.uri}, "range": target["range"], "context": {"diagnostics": [target]},
        })
        import_action = next(a for a in actions if a["title"] == "Import std::stats")
        self.assertEqual(import_action["edit"]["changes"][self.uri][0]["newText"], "bring std::stats;\n")


if __name__ == "__main__":
    unittest.main()
