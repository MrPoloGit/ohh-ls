#!/usr/bin/env python3
"""End-to-end smoke test for ohh-ls over real LSP stdio framing.

Mirrors phh-ls/tests/lsp_smoke_test.py's shape exactly: initialize, open
every corpus file via textDocument/didOpen, read back publishDiagnostics,
request hover, request completion, didChange with a deliberately broken
edit, didClose, shutdown/exit.

Usage: python3 ohh-ls/tests/lsp_smoke_test.py <path-to-ohh-ls-binary> <registry-dir> <corpus-dir>
"""
import json
import subprocess
import sys


def encode(msg: dict) -> bytes:
    body = json.dumps(msg).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def read_message(stream) -> dict:
    headers = {}
    while True:
        line = stream.readline().decode("ascii")
        if line in ("\r\n", "\n", ""):
            break
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip()] = v.strip()
    length = int(headers["Content-Length"])
    body = stream.read(length)
    return json.loads(body)


def main():
    ohh_ls, registry_dir, corpus_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    import glob
    import os

    proc = subprocess.Popen(
        [ohh_ls, registry_dir],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    def send(msg):
        proc.stdin.write(encode(msg))
        proc.stdin.flush()

    failures = []

    def check(label, cond):
        status = "ok" if cond else "FAIL"
        print(f"[{status}] {label}")
        if not cond:
            failures.append(label)

    # 1. initialize / initialized
    send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}})
    resp = read_message(proc.stdout)
    check("initialize responds with id=1", resp.get("id") == 1)
    caps = resp.get("result", {}).get("capabilities", {})
    check("capabilities advertise hoverProvider", caps.get("hoverProvider") is True)
    check("capabilities advertise completionProvider", "completionProvider" in caps)
    check("capabilities advertise full-document textDocumentSync", caps.get("textDocumentSync") == 1)

    send({"jsonrpc": "2.0", "method": "initialized", "params": {}})

    # 2. didOpen every corpus file and check publishDiagnostics matches expectations.
    expected_zero = sorted(glob.glob(os.path.join(corpus_dir, "valid", "*.ohh")))
    expected_nonzero = sorted(glob.glob(os.path.join(corpus_dir, "invalid", "*.ohh")))

    for path in expected_zero + expected_nonzero:
        uri = "file://" + os.path.abspath(path)
        text = open(path, encoding="utf-8").read()
        send(
            {
                "jsonrpc": "2.0",
                "method": "textDocument/didOpen",
                "params": {"textDocument": {"uri": uri, "languageId": "ohh", "version": 1, "text": text}},
            }
        )
        note = read_message(proc.stdout)
        check(f"{os.path.basename(path)}: publishDiagnostics received",
              note.get("method") == "textDocument/publishDiagnostics")
        diags = note.get("params", {}).get("diagnostics", [])
        if path in expected_zero:
            check(f"{os.path.basename(path)}: zero diagnostics", len(diags) == 0)
        else:
            check(f"{os.path.basename(path)}: at least one diagnostic", len(diags) >= 1)

    # 3. hover on the game_type field's value in the real spec example (the
    # first file alphabetically among the valid fixtures:
    # holdem-cash-pokerstars.ohh). Position confirmed against the file's
    # real byte layout via `tree-sitter query` earlier in this session:
    # "Holdem" spans line 6, columns 21-29.
    sample = expected_zero[0]
    assert os.path.basename(sample) == "holdem-cash-pokerstars.ohh", sample
    uri = "file://" + os.path.abspath(sample)
    send(
        {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "textDocument/hover",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 6, "character": 25}},
        }
    )
    resp = read_message(proc.stdout)
    check("hover responds with id=2", resp.get("id") == 2)
    result = resp.get("result")
    check("hover on game_type field returns non-null markdown content",
          result is not None and "markdown" == result.get("contents", {}).get("kind"))
    check("hover text mentions the Holdem enum value",
          result is not None and "Holdem" in result.get("contents", {}).get("value", ""))

    # 4. completion returns a non-empty list including known field names and enum values.
    send(
        {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "textDocument/completion",
            "params": {"textDocument": {"uri": uri}, "position": {"line": 0, "character": 0}},
        }
    )
    resp = read_message(proc.stdout)
    items = resp.get("result", [])
    labels = {item["label"] for item in items}
    check("completion includes a top-level field (spec_version)", "spec_version" in labels)
    check("completion includes a nested-object field (starting_stack)", "starting_stack" in labels)
    check("completion includes a game_type enum value (Holdem)", "Holdem" in labels)
    check("completion includes an action enum value (Fold)", "Fold" in labels)

    # 5. didChange with a deliberately broken edit re-triggers validation.
    original = open(sample, encoding="utf-8").read()
    broken = original.replace('"game_type": "Holdem"', '"game_type": "Omahaa"')
    assert broken != original
    send(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didChange",
            "params": {
                "textDocument": {"uri": uri, "version": 2},
                "contentChanges": [{"text": broken}],
            },
        }
    )
    note = read_message(proc.stdout)
    diags = note.get("params", {}).get("diagnostics", [])
    check("didChange to an unknown enum value re-validates with a new diagnostic", len(diags) >= 1)
    check("didChange diagnostic is bad-enum-value", any(d.get("code") == "bad-enum-value" for d in diags))

    # 6. didClose clears diagnostics.
    send({"jsonrpc": "2.0", "method": "textDocument/didClose", "params": {"textDocument": {"uri": uri}}})
    note = read_message(proc.stdout)
    check("didClose publishes an empty diagnostics list", note.get("params", {}).get("diagnostics") == [])

    # 7. shutdown / exit.
    send({"jsonrpc": "2.0", "id": 4, "method": "shutdown", "params": None})
    resp = read_message(proc.stdout)
    check("shutdown responds with id=4 and null result", resp.get("id") == 4 and resp.get("result") is None)
    send({"jsonrpc": "2.0", "method": "exit", "params": None})
    proc.wait(timeout=5)
    check("process exits cleanly after exit notification", proc.returncode == 0)

    print()
    if failures:
        print(f"{len(failures)} FAILURE(S):")
        for f in failures:
            print(" -", f)
        sys.exit(1)
    print("All checks passed.")


if __name__ == "__main__":
    main()
