#!/usr/bin/env python3
"""Feature × position matrix: each feature, reached through each kind of receiver, used in each kind of
position, compiled and run on the Z-machine and Glulx.

Two tables:
  read   a value-producing feature (`R.n`, `R.acc`, `R?.get()`, …) in each expression position
         (initializer, condition, interpolation, loop, try body, lambda, argument, …).
  write  a store (`R.n = 9`, `R.acc = 4`, `R.n++`, `R.items += 8`, …) in each statement position
         (plain, if body, loop body, try body, lambda body, switch case), read back through the global.

One program per (table, feature, receiver, target). Every position prints `[name:value]`, checked against the
value the feature is known to produce, so a position that compiles but computes the wrong thing fails as
surely as one that doesn't compile.

Usage: feature_matrix.py [--only [TABLE/]FEATURE[:RECEIVER]] [--target z5|glulx] [--positions a,b]
                         [--keep DIR] [--why] [--list]
Exit status is non-zero when any program fails.
"""
import argparse, concurrent.futures, os, re, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from spec_doctest import try_program  # noqa: E402

FIXTURE = '''#include <array>
value class doubler {
    int _v = 0;
    int  operator ()        { return _v; }
    void operator = (int v) { _v = v * 2; }
}
class Box : object {
    int n = 7;
    emitter int twice { ($self.n * 2) }
    doubler acc;
    array<int> items[6];
    int get() { return n; }
    Box me() { return self; }
    int scaled(int by = 1, int plus = 0) { return n * by + plus; }
    int operator + (int k) { return n + k; }
}
class Holder : object { Box item; Holder inner; }
Box gb { }
Holder gh { item = gb; }
Holder gh2 { inner = gh; }
array<Box> boxes = {gb};
Box getBox() { return gb; }
int ident(int x) { return x; }
void reset() { gb.n = 7; gb.acc = 5; gb.items = {4, 5, 6}; }
'''

# `@` is the receiver.
READS = {                       # name → (expression, value it produces)
    'member':    ('@.n',                       7),
    'emitval':   ('@.twice',                   14),
    'accessor':  ('@.acc',                     10),
    'optional':  ('@?.n',                      7),
    'optacc':    ('@?.acc',                    10),
    'optemit':   ('@?.twice',                  14),
    'optcall':   ('@?.get()',                  7),
    'method':    ('@.get()',                   7),
    'callmember': ('@.me().n',                 7),
    'named':     ('@.scaled(plus: 1, by: 2)',  15),
    'operator':  ('(@ + 3)',                   10),
    'arrlen':    ('@.items.length',            3),
    'arrelem':   ('@.items[1]',                5),
}

WRITES = {                      # name → (statement, read-back expression on gb, value read back)
    'set':       ('@.n = 9;',          'gb.n',          9),
    'setacc':    ('@.acc = 4;',        'gb.acc',        8),
    'compound':  ('@.n += 2;',         'gb.n',          9),
    'postinc':   ('@.n++;',            'gb.n',          8),
    'preinc':    ('++@.n;',            'gb.n',          8),
    'arrappend': ('@.items += 8;',     'gb.items[3]',   8),
    'arrbrace':  ('@.items += {8};',   'gb.items.length', 4),
    'arrset':    ('@.items[0] = 9;',   'gb.items[0]',   9),
}

# The member-array features run a second time with a global object named like the array member, so
# the member emits under its renamed I6 property (`_m_items`) and every access path must use that name.
CLASH_FEATURES = {'arrlen', 'arrelem', 'arrappend', 'arrbrace', 'arrset'}
CLASH_FIXTURE = 'object Items { }\n'

# name → receiver text; `l` is a local and `p` a parameter, both bound to gb in every function, and
# `self` is gb when the probe runs as a Box method.
RECEIVERS = {
    'global':    'gb',
    'local':     'l',
    'param':     'p',
    'self':      'self',
    'member':    'gh.item',
    'subscript': 'boxes[0]',
    'call':      'getBox()',
    'chained':   'gh2.inner.item',
}


def tok(name, expr):
    return f'print("[{name}:"); print({expr}); print("]");'


def read_positions(E, V):
    """(position, Beguile statements using E, expected printed values in order)."""
    return [
        ('decl',     f'int v1 = {E}; ' + tok('decl', 'v1'),                                [V]),
        ('assign',   f'int v2; v2 = {E}; ' + tok('assign', 'v2'),                          [V]),
        ('print',    tok('print', E),                                                      [V]),
        ('cond',     f'if ({E} == {V}) print("[cond:1]"); else print("[cond:0]");',        [1]),
        ('interp',   f'print($"[interp:{{{E}}}]");',                                       [V]),
        ('arg',      tok('arg', f'ident({E})'),                                            [V]),
        ('namedarg', tok('namedarg', f'ident(x: {E})'),                                    [V]),
        ('loop',     f'for (int i in 1 to 2) {{ {tok("loop", E)} }}',                      [V, V]),
        ('while',    f'int k = 0; while (k < {E}) k++; ' + tok('while', 'k'),              [V]),
        ('dowhile',  f'int d = 0; do {{ d++; }} while (d < {E}); ' + tok('dowhile', 'd'),  [V]),
        ('switch',   f'switch ({E}) {{ case {V}: print("[switch:1]"); default: print("[switch:0]"); }}', [1]),
        ('try',      f'try {{ {tok("try", E)} }} catch (int e) {{ print("[try:caught]"); }}', [V]),
        ('lambda',   f'func<int> fl = => {E}; ' + tok('lambda', 'fl()'),                   [V]),
        ('twice',    tok('twice', f'{E} + {E}'),                                           [2 * V]),
        ('ternary',  tok('ternary', f'{E} > 0 ? {E} : 0'),                                 [V]),
        ('compound', f'int c = 1; c += {E}; ' + tok('compound', 'c'),                      [V + 1]),
        ('literal',  f'array<int> t = {{{E}, 1}}; ' + tok('literal', 't[0]'),              [V]),
        ('return',   tok('return', 'retIt(p)'),                                            [V]),
    ]


def write_positions(S, back, V):
    after = lambda name: tok(name, back) + ' reset();'
    return [
        ('stmt',    f'{S} ' + after('stmt'),                                                  [V]),
        ('if',      f'if (true) {{ {S} }} ' + after('if'),                                    [V]),
        ('loop',    f'for (int i in 1 to 1) {{ {S} }} ' + after('loop'),                      [V]),
        ('while',   f'int w = 0; while (w < 1) {{ {S} w++; }} ' + after('while'),             [V]),
        ('switch',  f'switch (1) {{ case 1: {S} }} ' + after('switch'),                       [V]),
        ('try',     f'try {{ {S} }} catch (int e) {{ print("[try:caught]"); }} ' + after('try'), [V]),
        ('lambda',  f'func<void> fw = => {{ {S} }}; fw(); ' + after('lambda'),                [V]),
    ]


ONLY_POSITIONS = None


def program(table, feature, receiver, clash=False):
    R = RECEIVERS[receiver]
    if table == 'read':
        fexpr, V = READS[feature]
        E = fexpr.replace('@', R)
        pos = read_positions(E, V)
        ret = f'int retIt(Box p) {{ Box l = gb; return {E}; }}'
    else:
        stmt, back, V = WRITES[feature]
        pos = write_positions(stmt.replace('@', R), back, V)
        ret = ''
    pos = [p for p in pos if ONLY_POSITIONS is None or p[0] in ONLY_POSITIONS]
    body = '\n    '.join(code for _, code, _ in pos)
    probe = f'{ret}\nvoid body(Box p) {{\n    Box l = gb;\n    {body}\n}}\n'
    fixture = FIXTURE + (CLASH_FIXTURE if clash else '')
    if receiver == 'self':      # the probe runs as a method, where `self` is gb
        src = fixture + 'extend class Box {\n' + probe + '}\n' + \
            'void main(){\n    bglInit();\n    reset();\n    gb.body(gb);\n}\n'
    else:
        src = fixture + probe + 'void main(){\n    bglInit();\n    reset();\n    body(gb);\n}\n'
    want = [(name, str(v)) for name, _, vals in pos for v in vals]
    return src, want


TOKEN = re.compile(r'\[(\w+):([^\]]*)\]')


def check(job):
    table, feature, receiver, target, keep, why, clash = job
    src, want = program(table, feature, receiver, clash)
    suffix = '_clash' if clash else ''
    work = os.path.join(keep or tempfile.mkdtemp(prefix='matrix-'), f'{table}_{feature}_{receiver}_{target}{suffix}')
    stage, detail, out = try_program(src, target, work)
    label = f'{table}/{feature}{"+clash" if clash else ""}:{receiver} [{target}]'
    if stage != 'ok':
        return label, f'{stage.upper()}-FAIL', detail
    got = TOKEN.findall(out)
    bad = []
    for i, (name, value) in enumerate(want):
        g = got[i] if i < len(got) else None
        if g != (name, value):
            bad.append(f'{name}: want {value}, got {g[1] if g and g[0] == name else g}')
    if bad:
        return label, 'MISMATCH', '; '.join(bad) + ('' if not why else '\n      ' + out.strip()[:400])
    return label, 'PASS', ''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', help='[read/|write/]FEATURE[:RECEIVER]')
    ap.add_argument('--target', choices=['z5', 'glulx'])
    ap.add_argument('--keep')
    ap.add_argument('--why', action='store_true', help='print the program output with each mismatch')
    ap.add_argument('--list', action='store_true', help='print a generated program instead of running')
    ap.add_argument('--positions', help='comma-separated positions to keep (for narrowing a failure)')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()
    global ONLY_POSITIONS
    if a.positions:
        ONLY_POSITIONS = set(a.positions.split(','))
    cells = [('read', f) for f in READS] + [('write', f) for f in WRITES]
    recvs = list(RECEIVERS)
    if a.only:
        spec, _, r = a.only.partition(':')
        tbl, _, f = spec.rpartition('/')
        cells = [c for c in cells if c[1] == f and (not tbl or c[0] == tbl)]
        if r:
            recvs = [r]
    if a.list:
        print(program(cells[0][0], cells[0][1], recvs[0])[0])
        return
    targets = [a.target] if a.target else ['z5', 'glulx']
    jobs = [(t, f, r, tg, a.keep, a.why, False) for t, f in cells for r in recvs for tg in targets]
    jobs += [(t, f, r, tg, a.keep, a.why, True) for t, f in cells if f in CLASH_FEATURES for r in recvs for tg in targets]
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        results = list(ex.map(check, jobs))
    counts = {}
    for label, status, detail in sorted(results):
        counts[status] = counts.get(status, 0) + 1
        if status != 'PASS':
            print(f'  {status}: {label} — {detail}')
    print('Feature matrix: ' + ', '.join(f'{v} {k.lower()}' for k, v in sorted(counts.items()))
          + f' ({len(READS)} reads × {len(read_positions("x", 1))} positions, '
          f'{len(WRITES)} writes × {len(write_positions("x;", "x", 1))} positions; '
          f'{len(RECEIVERS)} receivers × {len(targets)} targets; array features again with a name clash)')
    sys.exit(0 if all(s == 'PASS' for _, s, _ in results) else 1)


if __name__ == '__main__':
    main()
