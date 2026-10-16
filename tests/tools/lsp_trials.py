#!/usr/bin/env python3
"""LSP trials: drive `beguiler --lsp` over every field trial and check what an editor would see.

Each trial's main file is opened on its own; each file in its `<name>_parts/` folder is opened with
the main file as the entry point (as the extension does after Set Entry Point). For each file:

  diagnostics  a valid program publishes no errors (warnings are listed, not failed)
  hover        a sample of identifier occurrences (members after '.', calls, declared names, type
               names, others) each hover to non-empty text naming the identifier, of a kind that fits
               the declaration go-to-definition finds (a class hovers as a class, an enum as an enum, a
               local or parameter is declared above its use in the same top-level declaration, a member
               access never hovers as a local)
  definition   the same sample each go to a location in an existing file whose line holds the name
  completion   after `x.` for a sample of member accesses, the member used there is offered
  liveness     the server answers every request within the timeout and stays alive

The sample skips text the server doesn't read as Beguile: comments, strings (and interpolation slots),
directives and directive bodies (#i6, #emitLast, …), emitter bodies, grammar/synonyms lists, inactive
#if branches, reserved words, I6 names (after asI6, before asBgl), member accesses whose receiver isn't
a plain dotted name (`f().x`, `a[i].x`), and `o.p` where p is a `property` variable.

Usage: lsp_trials.py [--only NAME] [--limit N] [--jobs N] [--timeout S] [--show]
  --only NAME   trials whose name contains NAME
  --limit N     at most N sampled identifiers per file (default 25)
  --show        print every request and its answer
Exit status is non-zero when any check fails.
"""
import argparse, collections, concurrent.futures, glob, json, os, queue, re, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
TRIALS = os.path.join(ROOT, 'tests', 'fieldTrials')
BIN = os.path.join(ROOT, 'beguiler')

# Appendix A reserved words, plus words the sample has no use for (directive-like and soft words).
RESERVED = set(w.lower() for w in '''
    additive alias array asBgl asI6 attribute auto bnum bool break case catch char charLiteral class const
    continue default delete dictionaryWord dictionaryWordLiteral do else emitter enum explicit extend extern
    false float for func hide if in inject inline int interpolatedStringLiteral intLiteral move
    negativeIntLiteral new nothing null object operator outer primitive property rawArray ref remove replace
    replaced required return rfalse rtrue self static string stringLiteral struct superposed switch synonyms throw
    to true try typesealed uint union until var verb void while this literal'''.split())


# ── Lexing and sampling ───────────────────────────────────────────────────────────────────────────

Tok = collections.namedtuple('Tok', 'kind text line col')   # kind: id, num, p (punctuation); line 0-based


def lex(text):
    """Tokens of Beguile code, leaving out comments, strings, char literals, directive lines and directive
    bodies (`#i6 { … }`, `#storedEmitLast name { … }`, …). Returns (tokens, skipped line set)."""
    toks, skipped = [], set()
    i, line, col, n = 0, 0, 0, len(text)

    def adv(k):
        nonlocal i, line, col
        for _ in range(k):
            if i < n and text[i] == '\n': line += 1; col = 0
            else: col += 1
            i += 1

    def skip_braces():   # from just before an opening '{' (whitespace allowed) to past its match
        while i < n and text[i] in ' \t\r\n': adv(1)
        if i >= n or text[i] != '{': return False
        depth = 0
        while i < n:
            c = text[i]
            if c == '{': depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0: adv(1); return True
            elif c == '"':
                adv(1)
                while i < n and text[i] != '"': adv(2 if text[i] == '\\' else 1)
            skipped.add(line); adv(1)
        return True

    while i < n:
        c = text[i]
        if c in ' \t\r\n': adv(1); continue
        if text.startswith('//', i):
            while i < n and text[i] != '\n': adv(1)
            continue
        if text.startswith('/*', i):
            j = text.find('*/', i + 2); adv((j + 2 if j >= 0 else n) - i); continue
        if c == '"':
            adv(1)
            while i < n and text[i] != '"': adv(2 if text[i] == '\\' else 1)
            adv(1); continue
        if text.startswith('$"', i):   # an interpolated string: a {slot} may hold strings of its own
            adv(2); depth = 0
            while i < n and not (text[i] == '"' and depth == 0):
                if text[i] == '\\': adv(2); continue
                if text[i] == '{': depth += 1
                elif text[i] == '}': depth = max(0, depth - 1)
                elif text[i] == '"':
                    adv(1)
                    while i < n and text[i] not in '"\n': adv(2 if text[i] == '\\' else 1)
                adv(1)
            adv(1); continue
        if c == "'":
            adv(1)
            while i < n and text[i] not in "'\n": adv(2 if text[i] == '\\' else 1)
            adv(1); continue
        if c == '#' and (i + 1 < n and text[i + 1].isalpha()):
            m = re.match(r'#(\w+)', text[i:]); word = m.group(1).lower()
            start_line = line
            adv(len(m.group(0)))
            # a directive with a body (`#i6 {`, `#emitLast {`, `#storedEmitLast name {`, `#beguilerSettings {`)
            mb = re.match(r'[ \t]*(\w+[ \t]*)?(\r?\n[ \t]*)?\{', text[i:])
            if mb and word not in ('if', 'elif'):
                adv(mb.end() - 1); skip_braces(); continue
            while i < n and text[i] != '\n': adv(1)
            skipped.add(start_line)
            continue
        if c.isalpha() or c == '_':
            j = i
            while j < n and (text[j].isalnum() or text[j] == '_'): j += 1
            toks.append(Tok('id', text[i:j], line, col)); adv(j - i); continue
        if c.isdigit() or (c == '$' and i + 1 < n and text[i + 1] in '$0123456789abcdefABCDEF'):
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] in '._$'): j += 1
            toks.append(Tok('num', text[i:j], line, col)); adv(j - i); continue
        two = text[i:i + 2]
        if two in ('::', '->', '=>', '+=', '-=', '==', '!=', '<=', '>=', ':=', '&&', '||', '++', '--'):
            toks.append(Tok('p', two, line, col)); adv(2); continue
        toks.append(Tok('p', c, line, col)); adv(1)
    return toks, skipped


def skip_regions(toks):
    """Indexes of tokens inside text the server doesn't read as Beguile: emitter bodies (all member bodies
    in an emitter class) and grammar/synonyms lists."""
    out = set()

    def match(k):   # index of the '}' closing the '{' at k
        d = 0
        for j in range(k, len(toks)):
            if toks[j].text == '{': d += 1
            elif toks[j].text == '}':
                d -= 1
                if d == 0: return j
        return len(toks) - 1

    k = 0
    while k < len(toks):
        t = toks[k]
        low = t.text.lower() if t.kind == 'id' else ''
        if low == 'emitter':
            emitter_class = k + 1 < len(toks) and toks[k + 1].text.lower() == 'class'
            j, paren = k + 1, 0
            while j < len(toks) and not (paren == 0 and toks[j].text in ('{', ';')):
                paren += toks[j].text == '('; paren -= toks[j].text == ')'; j += 1
            if j < len(toks) and toks[j].text == '{':
                e = match(j)
                if emitter_class:   # the class body is Beguile; every member body inside it is I6
                    m = j + 1
                    while m < e:
                        if toks[m].text == '{':
                            me = match(m); out.update(range(m, me + 1)); m = me
                        m += 1
                    k = e
                else:
                    out.update(range(j, e + 1)); k = e
        elif low in ('grammar', 'synonyms'):
            j = k + 1
            while j < len(toks) and toks[j].text in ('+=', '-=', '=', '+', '-'): j += 1
            if j > k + 1 and j < len(toks) and toks[j].text == '{':
                e = match(j); out.update(range(j, e + 1)); k = e
        k += 1
    return out


def candidates(text, inactive):
    """[(category, Tok, receiver)] for every identifier occurrence worth asking about."""
    toks, skipped = lex(text)
    bad = skip_regions(toks)
    # `o.p` with `property p` reads the property p names: p is a variable, not a member
    prop_vars = {m.lower() for m in re.findall(r'\bproperty\s+(\w+)', text)}
    out = []
    for k, t in enumerate(toks):
        if t.kind != 'id' or k in bad or t.line in skipped or t.text.lower() in RESERVED: continue
        if k and toks[k - 1].text.lower() == 'asi6': continue           # the I6 name a declaration emits as
        if t.text.lower() in ('first', 'last', 'before', 'after') and k >= 2 and \
           toks[k - 2].text.lower() in ('inject', 'move'): continue     # an array inject's position word
        if k + 1 < len(toks) and any(x.text.lower() == 'asbgl' for x in toks[k + 1:k + 12] if x.line == t.line) \
           and not (k and toks[k - 1].text.lower() == 'asbgl'):
            continue                                                    # an extern's I6 symbol, named before asBgl
        if any(s <= t.line < e for s, e in inactive): continue
        prev = toks[k - 1] if k else None
        nxt = toks[k + 1] if k + 1 < len(toks) else None
        adjacent = lambda a, b: a.line == b.line and a.col + len(a.text) == b.col
        receiver = None
        if prev and prev.text == '.':
            if not (k >= 2 and toks[k - 2].kind == 'id' and adjacent(toks[k - 2], prev) and adjacent(prev, t)):
                continue                                    # `.word` dictionary word, or `f().x`
            j, segs = k - 2, []
            while True:
                segs.insert(0, toks[j].text)
                if j >= 2 and toks[j - 1].text == '.' and adjacent(toks[j - 1], toks[j]):
                    if toks[j - 2].kind == 'id' and adjacent(toks[j - 2], toks[j - 1]): j -= 2; continue
                    segs = None                             # `a[i].x`, `f().x.y`
                break
            if segs is None: continue
            if j >= 1 and toks[j - 1].text in (')', ']') and adjacent(toks[j - 1], toks[j]): continue
            if t.text.lower() in prop_vars: continue
            receiver = '.'.join(segs)
            cat = 'member'
        elif prev and prev.text == '::':
            cat = 'scoped'
        elif prev and prev.kind == 'id' and prev.text.lower() in ('class', 'struct', 'enum', 'bnum', 'verb'):
            cat = 'decl'
        elif prev and (prev.kind == 'id' and prev.text.lower() not in ('return', 'new', 'print', 'case', 'in', 'throw', 'delete', 'else', 'do')
                       or prev.text == '>') and nxt and nxt.text in ('=', ';', '(', '{', ':', ',', ')', '['):
            cat = 'decl'
        elif nxt and nxt.text == '(':
            cat = 'call'
        elif nxt and nxt.kind == 'id' or (prev and prev.text.lower() in ('new', ':')):
            cat = 'type'
        else:
            cat = 'other'
        out.append((cat, t, receiver))
    return out


def sample(cands, limit):
    """Up to `limit` occurrences: round robin over the categories, distinct names first, spread through the file."""
    by = collections.OrderedDict((c, []) for c in ('member', 'decl', 'call', 'type', 'scoped', 'other'))
    for c in cands: by[c[0]].append(c)
    picks = {}
    for cat, lst in by.items():
        seen, first, rest = set(), [], []
        for c in lst:
            key = (c[2] or '').lower() + '.' + c[1].text.lower()
            (rest if key in seen else first).append(c); seen.add(key)
        def spread(xs, m):
            if len(xs) <= m: return list(xs)
            return [xs[int(i * len(xs) / m)] for i in range(m)]
        picks[cat] = spread(first, limit) + spread(rest, limit)
    out, idx = [], 0
    while len(out) < limit and any(idx < len(v) for v in picks.values()):
        for v in picks.values():
            if idx < len(v) and len(out) < limit: out.append(v[idx])
        idx += 1
    return sorted(out, key=lambda c: (c[1].line, c[1].col))


# ── The server ───────────────────────────────────────────────────────────────────────────────────

class Server:
    def __init__(self, cwd, timeout):
        self.p = subprocess.Popen([BIN, '--lsp'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, cwd=cwd)
        self.timeout, self.q, self.next_id = timeout, queue.Queue(), 1
        self.notes = collections.defaultdict(dict)   # method -> uri -> params
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        out = self.p.stdout
        while True:
            h = b''
            while not h.endswith(b'\r\n\r\n'):
                c = out.read(1)
                if not c: self.q.put(None); return
                h += c
            length = int([l for l in h.split(b'\r\n') if l.lower().startswith(b'content-length')][0].split(b':')[1])
            m = json.loads(out.read(length))
            if 'id' in m: self.q.put(m)
            elif 'method' in m: self.notes[m['method']][m['params'].get('uri')] = m['params']

    def send(self, method, params, request=False):
        o = {'jsonrpc': '2.0', 'method': method, 'params': params}
        if request: o['id'] = self.next_id; self.next_id += 1
        b = json.dumps(o).encode()
        try:
            self.p.stdin.write(b'Content-Length: %d\r\n\r\n' % len(b) + b); self.p.stdin.flush()
        except (BrokenPipeError, OSError):
            raise ServerDied('crash')
        return o.get('id')

    def request(self, method, params):
        rid = self.send(method, params, True)
        t0 = time.time()
        while True:
            try: m = self.q.get(timeout=max(0.1, self.timeout - (time.time() - t0)))
            except queue.Empty: raise ServerDied('hang')
            if m is None: raise ServerDied('crash')
            if m.get('id') == rid: return m

    def wait_note(self, method, uri):
        t0 = time.time()
        while uri not in self.notes[method]:
            if self.p.poll() is not None: raise ServerDied('crash')
            if time.time() - t0 > self.timeout: raise ServerDied('hang')
            time.sleep(0.01)
        return self.notes[method][uri]

    def close(self):
        try: self.p.kill()
        except Exception: pass


class ServerDied(Exception):
    pass


# ── Checks ──────────────────────────────────────────────────────────────────────────────────────

def hover_text(res):
    r = res.get('result')
    if not r: return ''
    c = r.get('contents')
    if isinstance(c, dict): return c.get('value', '')
    if isinstance(c, list): return '\n'.join(x if isinstance(x, str) else x.get('value', '') for x in c)
    return c or ''


def signature(hover):
    """The hover's signature line (inside the ```bgl fence), or the whole text."""
    m = re.search(r'```bgl\n(.*?)\n```', hover, re.S)
    return (m.group(1) if m else hover).strip()


def top_level(lines, ln):
    """The line of the top-level declaration (a class, object, function or verb at column 0) holding line ln."""
    while ln > 0 and not (lines[ln][:1].strip() and not lines[ln].startswith(('}', '{', '//', '/*', '*', '#', ')'))):
        ln -= 1
    return ln


_file_cache = {}
def file_lines(path):
    if path not in _file_cache:
        try: _file_cache[path] = open(path, encoding='utf-8', errors='replace').read().split('\n')
        except OSError: _file_cache[path] = None
    return _file_cache[path]


def check_file(path, entry, args):
    """Run every check on one file. Returns (counts, failures, warnings, log)."""
    counts = collections.Counter()
    fails, warns, log = [], [], []
    rel = os.path.relpath(path, TRIALS)
    text = open(path, encoding='utf-8', errors='replace').read()
    lines = text.split('\n')
    uri = 'file://' + path
    srv = Server(os.path.dirname(entry or path), args.timeout)

    def fail(check, where, ident, what):
        fails.append((check, rel, where, ident, what))

    try:
        srv.request('initialize', {'rootUri': 'file://' + os.path.dirname(entry or path), 'capabilities': {}})
        srv.send('initialized', {})
        if entry: srv.send('beguile/setEntryPoint', {'uri': 'file://' + entry})
        srv.send('textDocument/didOpen', {'textDocument': {'uri': uri, 'languageId': 'beguile', 'version': 1, 'text': text}})
        diags = srv.wait_note('textDocument/publishDiagnostics', uri)['diagnostics']
        inactive = [(r['start']['line'], r['end']['line'])
                    for r in srv.wait_note('beguile/inactiveRegions', uri).get('ranges', [])]
        counts['diagnostics'] += 1
        errs = [d for d in diags if d.get('severity', 1) == 1]
        for d in diags:
            where = f"{d['range']['start']['line'] + 1}:{d['range']['start']['character']}"
            if d.get('severity', 1) == 1: fail('diagnostics', where, '', d['message'])
            else: warns.append((rel, where, d['message']))
        if errs: counts['diagnostics_fail'] += 1

        cands = candidates(text, inactive)
        picks = sample(cands, args.limit)
        for cat, t, recv in picks:
            where = f'{t.line + 1}:{t.col}'
            pos = {'textDocument': {'uri': uri}, 'position': {'line': t.line, 'character': t.col}}
            name = t.text.lower()
            # hover
            hv = hover_text(srv.request('textDocument/hover', pos))
            sig = signature(hv)
            log.append(f'{rel}:{where} [{cat}] {recv + "." if recv else ""}{t.text}  hover: {sig!r}')
            counts['hover'] += 1
            hover_bad = None
            alias = re.match(r'type alias → ([\w.]+)', sig)   # a namespace's type alias hovers (and goes) to its target
            alias = alias.group(1).lower() if alias else None
            if not hv: hover_bad = 'no hover'
            elif not re.search(r'(?<![\w])' + re.escape(name) + r'(?![\w])', sig.lower()) and not alias:
                hover_bad = f'hover names something else: {sig!r}'
            elif cat == 'member' and re.match(r'(local|param):', sig):
                hover_bad = f'member access hovers as a local: {sig!r}'
            # definition
            res = srv.request('textDocument/definition', pos).get('result')
            counts['definition'] += 1
            if isinstance(res, list): res = res[0] if res else None
            def_bad, def_line = None, None
            if not res:
                def_bad = 'no definition'
            else:
                dpath = res['uri'][len('file://'):] if res['uri'].startswith('file://') else res['uri']
                dl = res['range']['start']['line']
                flines = file_lines(dpath)
                dshow = f'{os.path.relpath(dpath, ROOT)}:{dl + 1}'
                log.append(f'    definition: {dshow}')
                if flines is None: def_bad = f'definition in a missing file: {dshow}'
                elif not 0 <= dl < len(flines): def_bad = f'definition past the end of the file: {dshow}'
                else:
                    def_line = flines[dl]
                    if not any(re.search(r'(?<![\w])' + re.escape(x) + r'(?![\w])', def_line.lower()) for x in (name, alias) if x):
                        def_bad = f'definition line lacks the name: {dshow}: {def_line.strip()[:100]!r}'
                if def_line is not None and hv and not hover_bad and not alias:
                    # the hover's kind should fit the declaration found
                    dlow = def_line.lower()
                    nm = re.escape(name)
                    if re.search(r'\b(class)\s+' + nm + r'\b', dlow) and not re.search(r'\bclass\b', sig.lower()) \
                       and not re.search(r'\balias\b', dlow):
                        hover_bad = f'declared as a class ({dshow}) but hovers as {sig!r}'
                    elif re.search(r'\b(enum|bnum)\s+' + nm + r'\b', dlow) and not re.match(r'(extern )?(enum|bnum) ', sig.lower()):
                        hover_bad = f'declared as an enum ({dshow}) but hovers as {sig!r}'
                    elif re.match(r'(local|param):', sig) and (os.path.realpath(dpath) != os.path.realpath(path) or dl > t.line
                                                              or top_level(lines, dl) != top_level(lines, t.line)):
                        hover_bad = f'hovers as {sig!r} declared at {dshow}, outside the declaration it is used in'
            ident = f'{recv + "." if recv else ""}{t.text} [{cat}]'
            if hover_bad: fail('hover', where, ident, hover_bad); counts['hover_fail'] += 1
            if def_bad: fail('definition', where, ident, def_bad); counts['definition_fail'] += 1

        # completion after `x.`
        members = [c for c in cands if c[0] == 'member']
        seen, comp = set(), []
        for c in members:
            key = (c[2].lower(), c[1].text.lower())
            if key not in seen: seen.add(key); comp.append(c)
        if len(comp) > args.limit // 2:
            m = args.limit // 2
            comp = [comp[int(i * len(comp) / m)] for i in range(m)]
        for _, t, recv in comp:
            where = f'{t.line + 1}:{t.col}'
            res = srv.request('textDocument/completion', {'textDocument': {'uri': uri},
                              'position': {'line': t.line, 'character': t.col}}).get('result') or []
            items = res['items'] if isinstance(res, dict) else res
            labels = {re.split(r'[(\s]', x['label'], maxsplit=1)[0].lower() for x in items}
            counts['completion'] += 1
            log.append(f'{rel}:{where} complete {recv}. -> {len(items)} items, has {t.text}: {t.text.lower() in labels}')
            if t.text.lower() not in labels:
                counts['completion_fail'] += 1
                shown = ', '.join(sorted(labels)[:8]) + (' …' if len(labels) > 8 else '')
                fail('completion', where, recv + '.' + t.text, f'{len(items)} items, member missing' + (f' ({shown})' if items else ''))
        if srv.p.poll() is not None: raise ServerDied('crash')
        counts['liveness'] += 1
    except ServerDied as e:
        counts['liveness'] += 1; counts['liveness_fail'] += 1
        fail('liveness', '', '', f'server {e} ({"exit " + str(srv.p.poll()) if srv.p.poll() is not None else "no answer in " + str(args.timeout) + "s"})')
    finally:
        srv.close()
    return counts, fails, warns, log


def files(only):
    out = []
    for main in sorted(glob.glob(os.path.join(TRIALS, '*.bgl'))):
        name = os.path.basename(main)[:-4]
        if name.startswith('_') or (only and only not in name): continue
        out.append((main, None))
        for part in sorted(glob.glob(os.path.join(TRIALS, name + '_parts', '**', '*.bgl'), recursive=True)):
            out.append((part, main))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--only'); ap.add_argument('--limit', type=int, default=25)
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    ap.add_argument('--timeout', type=float, default=30); ap.add_argument('--show', action='store_true')
    args = ap.parse_args()
    todo = files(args.only)
    t0 = time.time()
    total, fails, warns = collections.Counter(), [], []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        for c, f, w, log in ex.map(lambda pe: check_file(pe[0], pe[1], args), todo):
            total.update(c); fails += f; warns += w
            if args.show: print('\n'.join(log))
    order = ['diagnostics', 'hover', 'definition', 'completion', 'liveness']
    if warns:
        print(f'warnings ({len(warns)}):')
        for rel, where, msg in sorted(warns): print(f'  {rel}:{where}: {msg}')
    for check in order:
        fs = [f for f in fails if f[0] == check]
        if not fs: continue
        print(f'{check} failures ({len(fs)}):')
        for _, rel, where, ident, what in sorted(fs, key=lambda f: (f[1], f[2])):
            print(f'  {rel}:{where}' + (f' {ident}' if ident else '') + f': {what}')
    print(f'{len(todo)} files in {time.time() - t0:.0f}s')
    for check in order:
        unit = 'files' if check in ('diagnostics', 'liveness') else 'requests'
        print(f'  {check:12} {total[check] - total[check + "_fail"]}/{total[check]} {unit} pass')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
