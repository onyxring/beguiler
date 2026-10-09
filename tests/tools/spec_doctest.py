#!/usr/bin/env python3
"""Doc tests: compile and run every `bgl` code block in the language spec.

Each block is wrapped into a program, compiled for the Z-machine (then Glulx, if that fails), run, and any
result the spec states with `// → value` is checked. A block that isn't meant to compile carries a marker in an
HTML comment on the line before its fence:

    <!-- doctest: skip -->      syntax sketch or fragment that needs context; not compiled
    <!-- doctest: error -->     must FAIL to compile (an example of a rejected construct)
    <!-- doctest: glulx -->     Glulx only; compiled and run for Glulx alone
    <!-- doctest: compile -->   compiled (through Inform 6) but not run: it needs state a bare program lacks
    <!-- doctest: pending reason -->  not compiled: the example and the compiler disagree, awaiting a decision;
                                listed on every run so it isn't forgotten

A line whose comment says `compile-time error` is left out of the program, and is checked on its own to be
rejected.

Usage: spec_doctest.py [--verbose] [--keep DIR] [--only FILE[:LINE]] [spec files…]
Exit status is non-zero when any block fails.
"""
import argparse, concurrent.futures, glob, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
SPEC = os.path.join(ROOT, 'docs', 'spec')
BEGUILER = os.path.join(ROOT, 'beguiler')
INFORM6 = os.environ.get('INFORM6') or shutil.which('inform6') or os.path.join(ROOT, '..', 'inform6', 'inform6')
ZVM = os.environ.get('ZVM') or shutil.which('zvm') or os.path.join(ROOT, '..', 'beguilex', 'node_modules', '.bin', 'zvm')
GLULX_RUN = os.path.join(HERE, 'glulx-run.js')
GLULX_CONSOLE = os.path.join(ROOT, 'tests', 'run', 'glulxConsole.bgl')
WHY = False
STDLIB = os.path.normpath(os.path.join(ROOT, '..', 'inform6', 'stdlib'))

DECL_START = re.compile(
    r'^\s*(#|class\b|object\b|enum\b|bnum\b|extern\b|emitter\b|verb\b|attribute\b|property\b|const\b|extend\b|'
    r'alias\b|superposed\b|static\b|replace\b|union\b|value\s+class\b|primitive\s+class\b|namespace\b|'
    r'additive\b|inline\b|typesealed\b|default\b|abstract\b)')
# `Type name(params) {` — a function; `Type name {` — an object of a class; `Type name[...]`/`= ...;` at top level
FUNC_START = re.compile(r'^\s*[A-Za-z_][\w<>,\s|]*\s+[A-Za-z_]\w*\s*\([^;]*\)\s*(\{|$)')
INSTANCE_START = re.compile(r'^\s*[A-Za-z_][\w<>]*\s+[A-Za-z_]\w*\s*\{')
DECL_VALUE = re.compile(r'^\s*(?:const\s+)?([A-Za-z_][\w<>,]*(?:<[^;=]*>)?)\s+([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*=\s*.*;\s*//\s*→\s*(.+)$')
PRINT_VALUE = re.compile(r'^\s*(?:print|for|if|try|while)\b.*;\s*(?:\})?\s*//\s*→\s*(.+)$')
VAR_DECL = re.compile(r'^\s*(?:const\s+)?[A-Za-z_][\w]*(?:<[^;=]*>)?\s+[A-Za-z_]\w*\s*(?:\[[^\]]*\])?\s*(?:=|;)')
STATEMENT_KEYWORDS = re.compile(r'^\s*(print|printLine|if|for|while|do|switch|return|try|throw|rtrue|rfalse|break|continue)\b')


ERROR_LINE = re.compile(r'//.*\bcompile-time error\b|//\s*error\b', re.I)


def expected_text(raw):
    """The value a `// → v` comment states: up to an explanatory `:` or `(`, without quotes."""
    v = raw.strip()
    if '(' in v and not v.startswith('"') and not v.startswith('('):
        v = v.split('(', 1)[0].strip()
    if ':' in v and not v.startswith('"'):
        v = v.split(':', 1)[0].strip()
    if len(v) >= 2 and v[0] == v[-1] and v[0] in '"\'':
        v = v[1:-1]
    return v


def same_value(got, want):
    """Printed value matches the stated one: textually, or as numbers (`5.0` is `5.0000`)."""
    if got == want:
        return True
    try:
        return float(got) == float(want)
    except ValueError:
        return False


def symbolic(want):
    """A stated value that names a constant (`WORDSIZE`) rather than giving one."""
    return re.fullmatch(r'[A-Z_][A-Z0-9_]*', want) is not None


def blocks_in(path):
    text = open(path, encoding='utf-8').read()
    lines = text.split('\n')
    out, i = [], 0
    while i < len(lines):
        if lines[i].strip() == '```bgl':
            start = i + 1
            j = start
            while j < len(lines) and lines[j].strip() != '```':
                j += 1
            marker = ''
            k = i - 1
            while k >= 0 and not lines[k].strip():
                k -= 1
            m = re.match(r'\s*<!--\s*doctest:\s*(\w+)\b\s*(.*?)\s*-->', lines[k]) if k >= 0 else None
            reason = ''
            if m:
                marker, reason = m.group(1), m.group(2)
            out.append({'file': os.path.relpath(path, SPEC), 'line': start, 'code': '\n'.join(lines[start:j]),
                        'marker': marker, 'reason': reason})
            i = j + 1
        else:
            i += 1
    return out


def split_chunks(code):
    """Top-level chunks: a chunk runs until brace depth returns to 0 at a line ending in `;` or `}`."""
    chunks, cur, depth = [], [], 0
    for line in code.split('\n'):
        stripped = re.sub(r'"(\\.|[^"\\])*"', '""', line)
        stripped = re.sub(r"'(\\.|[^'\\])*'", "''", stripped)
        stripped = stripped.split('//', 1)[0]
        cur.append(line)
        depth += stripped.count('{') - stripped.count('}')
        s = stripped.rstrip()
        # A directive (`#using lib`) is a line of its own; it needs no `;`.
        directive = depth <= 0 and re.match(r'\s*#(using|include)\b', s)
        if depth <= 0 and (s.endswith(';') or s.endswith('}') or not s or directive):
            chunks.append('\n'.join(cur))
            cur, depth = [], 0
    if cur:
        chunks.append('\n'.join(cur))
    return [c for c in chunks if c.strip()]


def is_declaration(chunk):
    first = next((l for l in chunk.split('\n') if l.strip() and not l.strip().startswith('//')), '')
    if STATEMENT_KEYWORDS.match(first):
        return False
    return bool(DECL_START.match(first) or FUNC_START.match(first) or INSTANCE_START.match(first))


BARE_EXPR = re.compile(r'^(\s*)([^;{}]+?)\s*//\s*→\s*(.+)$')


def elided(code):
    """True when the block's code (outside comments and strings) elides something with an ellipsis."""
    for line in code.split('\n'):
        c = re.sub(r'"(\\.|[^"\\])*"', '""', line).split('//', 1)[0]
        c = re.sub(r'\s!\s.*$', '', c)   # an Inform 6 comment inside an emitter body
        if '…' in c:
            return True
    return False


def split_error_lines(code):
    """The block without its `// compile-time error` lines, and those lines."""
    keep, errors = [], []
    for line in code.split('\n'):
        (errors if ERROR_LINE.search(line) else keep).append(line)
    return '\n'.join(keep), errors


def instrument(code):
    """Add a print after each `T x = …; // → v` line, print a bare `expr // → v`, and collect the values."""
    out, expects, n = [], [], 0
    depth = 0
    for line in code.split('\n'):
        b = BARE_EXPR.match(line) if depth == 0 else None
        depth += line.split('//', 1)[0].count('{') - line.split('//', 1)[0].count('}')
        if b and not DECL_VALUE.match(line) and not PRINT_VALUE.match(line) and not b.group(2).strip().endswith(','):
            n += 1
            out.append(f'{b.group(1)}print($"[[{n}:{{{b.group(2).strip()}}}]]");')
            expects.append(('marked', n, expected_text(b.group(3))))
            continue
        out.append(line)
        d = DECL_VALUE.match(line)
        if d:
            n += 1
            out.append(f'print($"[[{n}:{{{d.group(2)}}}]]");')
            expects.append(('marked', n, expected_text(d.group(3))))
            continue
        p = PRINT_VALUE.match(line)
        if p:
            expects.append(('output', 0, expected_text(p.group(1))))
    return '\n'.join(out), expects


def has_main(code):
    return re.search(r'\b(void|int)\s+main\s*\(', code, re.I) is not None


def programs_for(code):
    """Candidate whole programs for a block, most faithful first."""
    if has_main(code):
        return [code]
    chunks = split_chunks(code)
    decls = [c for c in chunks if is_declaration(c)]
    stmts = [c for c in chunks if not is_declaration(c)]
    body = lambda parts: 'void main(){\n    bglInit();\n' + '\n'.join(parts) + '\n}\n'
    # A typed variable (`array<char> w = "hi";`) can be a global or a local; try both.
    # Only a constant initializer can live at file scope (Inform 6 has no load-time code).
    const_init = lambda c: re.search(r'=\s*(-?\d+|\$[0-9A-Fa-f]+|"[^"]*"|\{[^}]*\}|true|false|null|\'.\')\s*;', c) or '=' not in c
    var_decl = lambda c: VAR_DECL.match(c) is not None and not STATEMENT_KEYWORDS.match(c) and const_init(c)
    vars_ = [c for c in stmts if var_decl(c)]
    rest = [c for c in stmts if not var_decl(c)]
    cands = []
    if stmts:
        cands.append('\n'.join(decls) + '\n' + body(stmts))
        if vars_ and rest:
            cands.append('\n'.join(decls + vars_) + '\n' + body(rest))
    cands.append(code + '\nvoid main(){ bglInit(); }\n')
    cands.append(body(chunks))
    seen, uniq = set(), []
    for c in cands:
        if c not in seen:
            seen.add(c)
            uniq.append(c)
    return uniq


def settings(target):
    return f'#beguilerSettings {{ target = {target}; informName = "none"; }}\n'


def stdlib_game(src):
    """`src` inside a standard-library Z5 game: the library's names (light, location, NOUN, …) are declared."""
    src = src.replace('void main(){\n    bglInit();', 'void _doctestBody(){', 1).replace('void main(){ bglInit(); }', '')
    # A block's own entry point runs from the library's Initialise.
    src = re.sub(r'\bvoid\s+main\s*\(\s*\)', 'void _doctestBody()', src, count=1, flags=re.I)
    calls = '_doctestBody();' if '_doctestBody' in src else ''
    if re.search(r'\b(?:int|bool|void)\s+Initialise\s*\(', src):
        return (f'#beguilerSettings {{ target = z5; includePaths = "{STDLIB}"; }}\n'
                '#include <bindings/i6StandardLibrary>\n#includeI6 "parser"\n#includeI6 "verblib"\n'
                + src + '\n#includeI6 "grammar"\n')
    return (f'#beguilerSettings {{ target = z5; includePaths = "{STDLIB}"; }}\n'
            '#include <bindings/i6StandardLibrary>\n#includeI6 "parser"\n#includeI6 "verblib"\n'
            'object _doctestRoom { attributes = light; }\n' + src +
            f'\nint Initialise(){{ location = _doctestRoom; {calls} rfalse; }}\n#includeI6 "grammar"\n')


def run(cmd, cwd, stdin=''):
    try:
        p = subprocess.run(cmd, cwd=cwd, input=stdin, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=30)
        return p.returncode, p.stdout + p.stderr
    except subprocess.TimeoutExpired:
        return -1, 'TIMEOUT'


def first_error(text):
    for l in text.split('\n'):
        if 'ERROR' in l or 'Error' in l:
            return re.sub(r'^.*?/(?=[^/]*\.bgl:)', '', l.strip())[:220]
    return text.strip().split('\n')[-1][:220] if text.strip() else ''


def try_program(src, target, work, run_it=True):
    """Compile (beguiler + inform6) and run. Returns (stage, detail, output)."""
    os.makedirs(work, exist_ok=True)
    own = re.search(r'#beguilerSettings\s*\{[^}]*\}', src)
    if own:   # a complete program brings its own settings; it runs on the target they name
        # a block that names no target gets the language default, Glulx
        target = 'z5' if re.search(r'target\s*=\s*"?z', own.group(0), re.I) else 'glulx'
        src = src.replace(own.group(0), own.group(0)[:-1] + ' informName = "none"; }', 1)
    prog = settings(target) if target != 'stdlib' and not own else ''
    if target == 'stdlib':
        src = stdlib_game(src)
    if target == 'glulx':
        shutil.copy(GLULX_CONSOLE, os.path.join(work, 'glulxConsole.bgl'))
        prog += '#include "glulxConsole.bgl"\n'
        if 'bglInit();' in src and not own:
            src = src.replace('bglInit();', 'bglInit(); glulxConsole();', 1)
        else:   # a block's own entry point opens the console first
            src = re.sub(r'((?:void|int)\s+main\s*\(\s*\)\s*\{)', r'\1 glulxConsole();', src, count=1, flags=re.I)
    path = os.path.join(work, 'doc.bgl')
    open(path, 'w', encoding='utf-8').write(prog + src)
    code, out = run([BEGUILER, '-o', os.path.join(work, 'out'), path], work)
    inf = os.path.join(work, 'out', 'doc.bgl.transpiled.inf')
    if code != 0 or not os.path.exists(inf):
        return 'beguiler', first_error(out), ''
    story = os.path.join(work, 'doc.' + ('ulx' if target == 'glulx' else 'z5'))
    code, out = run([INFORM6, '-G' if target == 'glulx' else '-v5', inf, story], work)
    if not os.path.exists(story):
        return 'inform6', first_error(out), ''
    if not run_it:
        return 'ok', '', ''
    if target == 'glulx':
        code, out = run(['node', GLULX_RUN, story], work, '\n')
    else:
        code, out = run([ZVM, story], work, '\n')
    if 'Programming error' in out or 'fatal' in out.lower() or code == -1:
        return 'run', first_error(out) or out.strip()[:200], out
    return 'ok', '', out


def check(block, keep):
    if block['marker'] == 'skip':
        return block, 'SKIP', '', ''
    if block['marker'] == 'pending':
        return block, 'PENDING', block['reason'], ''
    if elided(block['code']):
        return block, 'SKIP', 'elided (…)', ''
    body, error_lines = split_error_lines(block['code'])
    code, expects = instrument(body)
    run_it = block['marker'] != 'compile'
    targets = ['glulx'] if block['marker'] == 'glulx' else ['z5', 'glulx', 'stdlib']
    work_root = keep or tempfile.mkdtemp(prefix='doctest-')
    tag = f"{block['file'].replace('.md', '')}_{block['line']}"
    best = None
    best_prog, best_target = '', ''
    for target in targets:
        for n, prog in enumerate(programs_for(code)):
            stage, detail, out = try_program(prog, target, os.path.join(work_root, tag, f'{target}{n}'), run_it)
            if WHY:
                print(f'    [{target} #{n}] {stage}: {detail}')
            if stage == 'ok' or (stage == 'run' and best is None):
                best = (stage, detail, out, target)
                best_prog, best_target = prog, target
                if stage == 'ok':
                    break
            elif best is None or best[0] == 'beguiler':
                best = (stage, detail, out, target) if best is None or stage != 'beguiler' else best
        if best and best[0] == 'ok':
            break
    stage, detail, out, target = best
    # Each `// compile-time error` line, added back to the program that worked, must be rejected.
    if error_lines and block['marker'] != 'error' and stage in ('ok', 'run'):
        good = best_prog
        for k, line in enumerate(error_lines):
            bad = re.sub(r'(void main\(\)\{\n    bglInit\(\);\n)', lambda m: m.group(1) + line + '\n', good, count=1) \
                  if 'void main(){\n    bglInit();' in good else good + '\n' + line + '\n'
            st2, _, _ = try_program(bad, best_target, os.path.join(work_root, tag, f'err{k}'), False)
            if st2 != 'beguiler':
                if not keep: shutil.rmtree(work_root, ignore_errors=True)
                return block, 'FAIL', f'not rejected: {line.strip()[:80]}', ''
    if not keep:
        shutil.rmtree(work_root, ignore_errors=True)
    if block['marker'] == 'error':
        return (block, 'PASS', 'rejected as expected', '') if stage == 'beguiler' else \
               (block, 'FAIL', 'expected a compile error, but it compiled', '')
    if stage == 'beguiler':
        return block, 'COMPILE-FAIL', detail, ''
    if stage == 'inform6':
        return block, 'I6-FAIL', detail, ''
    if stage == 'run':
        return block, 'RUN-FAIL', detail, out
    missing = []
    for kind, n, want in (expects if run_it else []):
        if symbolic(want):
            continue
        if kind == 'marked':
            m = re.search(rf'\[\[{n}:(.*?)\]\]', out)
            if not m or not same_value(m.group(1).strip(), want):
                missing.append(f'value {n}: want {want!r}, got {m.group(1) if m else None!r}')
        elif want and want not in out:
            missing.append(f'output: want {want!r}')
    if missing:
        return block, 'VALUE-MISMATCH', '; '.join(missing), out
    return block, 'PASS', target, ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='*')
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('--keep')
    ap.add_argument('--only')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    ap.add_argument('--why', action='store_true', help='print every wrapping attempt (use with --only)')
    a = ap.parse_args()
    global WHY
    WHY = a.why
    files = a.files or sorted(glob.glob(os.path.join(SPEC, '*.md')))
    blocks = [b for f in files for b in blocks_in(f)]
    if a.only:
        f, _, l = a.only.partition(':')
        blocks = [b for b in blocks if b['file'] == f and (not l or b['line'] == int(l))]
    results = []
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        for r in ex.map(lambda b: check(b, a.keep), blocks):
            results.append(r)
    counts = {}
    for b, status, detail, out in results:
        counts[status] = counts.get(status, 0) + 1
        if status not in ('PASS', 'SKIP') or a.verbose:
            print(f"  {status}: {b['file']}:{b['line']} — {detail}")
            if a.verbose and out and status != 'PASS':
                print('      ' + out.strip().replace('\n', '\n      ')[:600])
    print('Spec doc tests: ' + ', '.join(f'{v} {k.lower()}' for k, v in sorted(counts.items())))
    sys.exit(0 if all(s in ('PASS', 'SKIP', 'PENDING') for _, s, _, _ in results) else 1)


if __name__ == '__main__':
    main()
