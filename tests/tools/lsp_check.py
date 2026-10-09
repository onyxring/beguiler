#!/usr/bin/env python3
"""LSP checks: drive `beguiler --lsp` over a document and check its diagnostics or a completion.

Each case opens one document, optionally asks for a completion at line:col (1-based line,
0-based column), and checks the diagnostics published for it and the completion labels.
Exits non-zero when any case fails.
"""
import json, os, subprocess, sys, threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BIN = os.path.join(ROOT, 'beguiler')

CASES = [
    # A namespace object completes its own members, not the root's operators and plumbing.
    dict(name='namespace completion', doc='tests/lsp/namespace_completion.bgl', at=(4, 8),
         has=['asm', 'ui', 'util'], lacks=['==(_bglobject)', 'instancename', 'print()', 'operator()']),
    # A Glulx-only library file opened on its own parses under the default target.
    dict(name='glulxWindow.bgl alone', doc='beguiLib/glulxWindow.bgl', at=(1, 0), no_diagnostics=True),
]

def run_case(case):
    path = os.path.join(ROOT, case['doc'])
    uri = 'file://' + path
    p = subprocess.Popen([BIN, '--lsp'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, cwd=os.path.dirname(path))
    timer = threading.Timer(60, p.kill); timer.start()
    diags = []
    def send(o):
        b = json.dumps(o).encode()
        p.stdin.write(b'Content-Length: %d\r\n\r\n' % len(b) + b); p.stdin.flush()
    def recv(want):
        while True:
            h = b''
            while not h.endswith(b'\r\n\r\n'):
                c = p.stdout.read(1)
                if not c: return None
                h += c
            n = int([l for l in h.split(b'\r\n') if l.lower().startswith(b'content-length')][0].split(b':')[1])
            m = json.loads(p.stdout.read(n))
            if m.get('id') == want: return m
            if m.get('method') == 'textDocument/publishDiagnostics' and m['params']['uri'] == uri:
                diags[:] = [(d['range']['start']['line'] + 1, d['message']) for d in m['params']['diagnostics']]
    try:
        send({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize',
              'params': {'rootUri': 'file://' + os.path.dirname(path), 'capabilities': {}}})
        recv(1)
        send({'jsonrpc': '2.0', 'method': 'initialized', 'params': {}})
        send({'jsonrpc': '2.0', 'method': 'textDocument/didOpen', 'params': {'textDocument': {
              'uri': uri, 'languageId': 'beguile', 'version': 1, 'text': open(path).read()}}})
        line, col = case['at']
        send({'jsonrpc': '2.0', 'id': 2, 'method': 'textDocument/completion', 'params': {
              'textDocument': {'uri': uri}, 'position': {'line': line - 1, 'character': col}}})
        res = recv(2)
    finally:
        timer.cancel(); p.kill()
    if res is None: return ['no response from the server']
    result = res.get('result') or []
    labels = [x['label'] for x in (result['items'] if isinstance(result, dict) else result)]
    problems = []
    if case.get('no_diagnostics') and diags:
        problems += [f'diagnostic at line {l}: {m}' for l, m in diags[:5]]
    problems += [f'completion lacks {x!r}' for x in case.get('has', []) if x not in labels]
    problems += [f'completion offers {x!r}' for x in case.get('lacks', []) if x in labels]
    return problems

def main():
    failed = 0
    for case in CASES:
        problems = run_case(case)
        if problems:
            failed += 1
            print(f'  FAIL: {case["name"]}')
            for pr in problems: print(f'    {pr}')
    print(f'LSP checks: {len(CASES) - failed} pass, {failed} fail')
    return 1 if failed else 0

if __name__ == '__main__':
    sys.exit(main())
