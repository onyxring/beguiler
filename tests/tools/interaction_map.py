#!/usr/bin/env python3
"""Feature-interaction map: which pairs of language areas the passing programs exercise together.

Most defects the field trials find now come from two features meeting for the first time, so the
untested ground is the set of area PAIRS no program combines. This tool:

  * defines the areas (AREAS: a key, a description, and the source patterns that use the area);
  * scans every passing program — field trials (and their parts), run tests, positive suite tests —
    one top-level declaration at a time (a class, object, function or verb with its body), so a pair
    counts only when both areas meet in the same declaration; the target (Glulx, PunyInform) is a
    file-wide area that meets everything in the file;
  * reports pairs covered out of all pairs, and the areas with the most uncovered pairs.

  interaction_map.py                     summary
  interaction_map.py --uncovered [AREA]  list the uncovered pairs (optionally those of one area)
  interaction_map.py --record LABEL      append today's coverage to tests/defects/interaction_history.csv
  interaction_map.py --assign N          deal the uncovered pairs into N lists (one per trial writer)
  interaction_map.py --triples [...]     the same for triples among the FOCUS areas
"""
import argparse, csv, datetime, glob, itertools, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)
HISTORY = os.path.join(TESTS, 'defects', 'interaction_history.csv')

# key, description, patterns (any one marks the area), file-wide
AREAS = [
    ('value_class',    'value classes (owned instances, copy operator)',      [r'\bvalue\s+class\b'], False),
    ('primitive',      'primitive classes (newtypes)',                        [r'\bprimitive\s+class\b'], False),
    ('emitter_class',  'emitter classes and init/deinit lifecycles',          [r'\bemitter\s+class\b', r'\bdeinit\s*\('], False),
    ('pooled',         'pooled classes, new / delete',                        [r'\bclass\s+\w+\s*\[\s*\d+\s*\]', r'\bnew\s+[\w.]+\s*\(', r'\bdelete\s+\w'], False),
    ('multi_parent',   'a class with several parents',                        [r'\bclass\s+\w+\s*(\[[^]]*\]\s*)?:\s*[\w.]+\s*,'], False),
    ('ancestor_call',  'dispatch to an ancestor (cast to a base, Base::m)',   [r'\(\s*\(\s*[A-Z]\w*\s*\)\s*\w+\s*\)\s*[.\[]', r'\w::\w'], False),
    ('arith_op',       'arithmetic operator overloads (+ - * / %)',          [r'\boperator\s*(\+|-|\*|/|%)\s*\('], False),
    ('compare_op',     'comparison operator overloads (== != < > <=>)',       [r'\boperator\s*(==|!=|<=>|<|>)\s*\('], False),
    ('compound_op',    'compound and step operators (+= -= ++ --), ?, switch', [r'\boperator\s*(\+=|-=|\+\+|--|\?|switch)'], False),
    ('conversion',     'conversion operator () and implicit conversion',     [r'\boperator\s*\(\s*\)', r'\boperator\s+auto\b'], False),
    ('subscript_op',   'operator [] / []= on a class',                        [r'\boperator\s*\[\s*\]'], False),
    ('auto_accessor',  'inline accessors (auto x = { … })',                  [r'\bauto\s+\w+\s*=\s*\{', r'\bauto\s*\{'], False),
    ('outer',          'outer. in an accessor',                               [r'\bouter\.'], False),
    ('static',         'static members and methods',                          [r'\bstatic\s+(?!array\b)(?![\s)])\w'], False),   # not the `static` attribute
    ('static_array',   'static member arrays',                                [r'\bstatic\s+array\s*<'], False),
    ('ref',            'ref members, locals and parameters',                  [r'\bref\s+\w'], False),
    ('bind',           'binding with :=',                                     [r':='], False),
    ('lambda',         'lambdas and captures',                                [r'=>'], False),
    ('lambda_block',   'lambdas with a block body',                           [r'=>\s*\{'], False),
    ('func_value',     'func<> values, literal functions, calls through them', [r'\bfunc\s*<'], False),
    ('linq',           '<linq> chains',                                       [r'\.(filter|map|orderBy|any|all|take|skip|distinct|skipWhile|takeWhile)\s*\('], False),
    ('array',          'array<T> (tracked arrays)',                           [r'\barray\s*<', r'\brawArray\s*<'], False),
    ('array_param',    'array<T> parameters',                                 [r'[(,]\s*(ref\s+)?array\s*<[^;{()]*?>\s+\w+\s*[,)=]'], False),
    ('array_ops',      'array operations (append, clear, popEnd, sort, length =, += list)', [r'\.(append|clear|popEnd|popStart|insert|removeAt|removeValue|sort|reverse|indexOf|contains|enqueue|dequeue)\s*\(', r'\.length\s*=[^=]', r'\+=\s*\{'], False),
    ('class_elem_array', 'arrays of class instances',                         [r'\barray\s*<\s*(?!array\b|char\b|int\b|uint\b|bool\b|string\b|stringObj\b|var\b|float\b|object\b|func\b|literal\b)[\w.]+\s*>'], False),
    ('nested_array',   'arrays of arrays',                                    [r'\barray\s*<\s*array\s*<'], False),
    ('nested_literal', 'nested list initializers ({ {…}, … })',               [r'=\s*\{\s*\{'], False),
    ('byte_array',     'array<char>',                                         [r'\barray\s*<\s*char\s*>'], False),
    ('buf',            '<buf> (bgl.util.buf)',                                [r'bgl\.util\.buf', r'#include\s*<buf>'], False),
    ('strings',        'stringObj and <string>',                              [r'\bstringObj\b', r'#include\s*<string>'], False),
    ('string_array',   'arrays of strings or stringObj',                      [r'\barray\s*<\s*(array\s*<\s*)?(stringObj|string)\s*>'], False),
    ('interpolation',  'interpolated strings',                                [r'\$"'], False),
    ('float',          'floats',                                              [r'\bfloat\b', r'(?<![\w.])\d+\.\d+(?!\.)'], False),
    ('enums',          'enums and bnums',                                     [r'\benum\s+\w', r'\bbnum\s+\w'], False),
    ('union_var',      'unions, var and typeof',                              [r'\bunion\s+\w', r'\btypeof\s*\(', r'\bvar\s+\w'], False),
    ('namespaces',     'namespaces: alias … for, #using, dotted type paths',  [r'\balias\s+\w+\s+for\b', r'#using\b'], False),
    ('extension',      'extend class / extend extern class / extend object',  [r'\bextend\s+(extern\s+)?(class\s+)?\w'], False),
    ('replace',        'replace / default / replaced()',                      [r'\breplace\b', r'\bdefault\s+\w+\s+\w+\s*\(', r'\breplaced\s*\('], False),
    ('i6_island',      'I6 islands: #i6 and #bgl blocks',                     [r'#i6\b', r'#bgl\b'], False),
    ('extern',         'extern declarations',                                 [r'\bextern\s'], False),
    ('i6expr',         '$i6Expr / $i6Name in emitters',                       [r'\$i6Expr', r'\$i6Name'], False),
    ('emitter_fn',     'emitter functions and members',                       [r'\bemitter\s+(?!class\b)\w'], False),
    ('verbs',          'verbs, grammar, actions, perform',                    [r'\bverb\s+\w+\s*\{', r'\bgrammar\s*(\+?=)', r'\bperform\s*\('], False),
    ('switch',         'switch (ranges, guards, operator switch)',           [r'\bswitch\s*\('], False),
    ('for_in',         'for … in loops (arrays, children, ranges, lists)',    [r'\bfor\s*\([^;)]*\bin\b'], False),
    ('exceptions',     'try / catch / throw',                                 [r'\btry\s*\{', r'\bthrow\b'], False),
    ('directives',     '#if / #define / #once / #exit and compile-time symbols', [r'#if\b', r'#define\b', r'#once\b', r'#exit\b', r'#declare\b'], False),
    ('object_tree',    'object tree: parent, children, moves',                [r'\bparent\s*=', r'\bchildren\b', r'\.parent\b'], False),
    ('attributes',     'attributes and properties (has/give/provides)',       [r'\battributes\s*=', r'\.(has|give|ungive|provides)\s*\(', r'\bproperty\s+\w'], False),
    ('positional',     'positional (inline) members and inline objects',      [r'\binline\s+\w', r'[=(,]\s*[\w.]*\s*\{[^{};=]*,'], False),
    ('opt_chain',      'optional chaining ?.',                                [r'\?\.'], False),
    ('coalesce',       'null coalescing ??',                                  [r'\?\?'], False),
    ('postfix_query',  'postfix ? query',                                     [r'[\w)\]]\?\s*[);&|]'], False),
    ('casts',          'casts',                                               [r'\(\s*(int|uint|bool|char|float|string|object|var)\s*\)\s*[\w(\'"]'], False),
    ('recursion',      'recursive routines',                                  [], False),   # detected structurally below
    ('glulx',          'Glulx target (windows, styles, hyperlinks)',          [r'target\s*=\s*(eTarget\.)?glulx'], True),
    ('punyinform',     'PunyInform library',                                  [r'bindings/punyInform'], True),
]
KEYS = [a[0] for a in AREAS]
FILE_WIDE = {a[0] for a in AREAS if a[3]}
COMPILED = {a[0]: [re.compile(p, re.I) for p in a[2]] for a in AREAS}   # Beguile is case-insensitive
# Pairs that can't occur in one program.
IMPOSSIBLE = {frozenset(('glulx', 'punyinform')),
              frozenset(('float', 'punyinform')),   # floats are Glulx-only
              frozenset(('byte_array', 'linq')),   # <linq> on array<char> is a compile-time error
              frozenset(('extern', 'outer')),         # outer lives in an inline accessor on a Beguile object
              frozenset(('multi_parent', 'outer')),   # outer is an error in a class body
              frozenset(('auto_accessor', 'extern')),   # an extern object can't hold an inline accessor
              frozenset(('extension', 'multi_parent'))}   # an extension adds members, never parents
# Each impossible pair is held by a negative test (tests/_test_impossible_*.bgl) that checks its diagnostic.
ALL_PAIRS = [frozenset(p) for p in itertools.combinations(KEYS, 2) if frozenset(p) not in IMPOSSIBLE]

# Triples: once nearly every pair is covered, defects come from three areas meeting (an alias type inside
# a nested array inside a class). Counted among the areas the round-15/16 defects clustered in.
FOCUS = ['static_array', 'auto_accessor', 'outer', 'nested_array', 'namespaces', 'class_elem_array',
         'byte_array', 'string_array', 'postfix_query', 'lambda_block', 'subscript_op', 'array_param',
         'value_class', 'extension']
ALL_TRIPLES = [frozenset(t) for t in itertools.combinations(FOCUS, 3)
               if not any(frozenset(p) in IMPOSSIBLE for p in itertools.combinations(t, 2))]
TRIPLE_HISTORY = os.path.join(TESTS, 'defects', 'interaction_triples_history.csv')
# Every triple of the 57 areas. "Accounted for" = some passing program combines it, or it can't occur in
# valid code (an impossible pair inside it, or a triple listed here, each held by a negative test).
IMPOSSIBLE_TRIPLES = set()
ALL_AREA_TRIPLES = [frozenset(t) for t in itertools.combinations(KEYS, 3)
                    if not any(frozenset(p) in IMPOSSIBLE for p in itertools.combinations(t, 2))
                    and frozenset(t) not in IMPOSSIBLE_TRIPLES]
ALL_TRIPLE_HISTORY = os.path.join(TESTS, 'defects', 'interaction_all_triples_history.csv')


UP_TO_ROUND = None   # --as-of-round: only the style trials that existed by then (round 5 wrote `_1`)


def trial_round_ok(path):
    if UP_TO_ROUND is None or '/fieldTrials/' not in path:
        return True
    m = re.search(r'_idioms(?:_(\d+))?(?:_parts|\.bgl)', path) or re.search(r'_variety(?:_(\d+))?(?:_parts|\.bgl)', path)
    if not m:
        return True                       # rounds 1–4 named their trials by topic
    return int(m.group(1) or 1) + 4 <= UP_TO_ROUND


def corpus():
    """Passing programs: field trials (with parts), run tests, positive suite tests."""
    files = glob.glob(os.path.join(TESTS, 'fieldTrials', '**', '*.bgl'), recursive=True)
    files = [f for f in files if trial_round_ok(f)]
    files += glob.glob(os.path.join(TESTS, 'run', '*.bgl'))
    files += glob.glob(os.path.join(TESTS, 'test_*.bgl'))
    return sorted(f for f in files if not os.path.basename(f).startswith('_') and '/output/' not in f)


def strip_comments(text):
    """Comments out, strings kept (an interpolated string is a feature)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i); i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2); i = n if j < 0 else j + 2
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1]); i = j + 1
        else:
            out.append(c); i += 1
    return ''.join(out)


def units(text):
    """Top-level declarations: each runs to its closing brace (or ';' for a one-liner)."""
    res, depth, start = [], 0, 0
    for i, c in enumerate(text):
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                res.append(text[start:i + 1]); start = i + 1
        elif c == ';' and depth == 0:
            res.append(text[start:i + 1]); start = i + 1
    if text[start:].strip():
        res.append(text[start:])
    return [u for u in res if u.strip()]


def is_recursive(unit):
    """True when a routine or method in the unit calls itself (by name, or `self.name(`)."""
    for m in re.finditer(r'\b(\w+)\s*\([^(){};]*\)\s*\{', unit):
        name = m.group(1)
        if name in ('if', 'while', 'for', 'switch', 'catch', 'do'):
            continue
        depth, i = 1, m.end()
        while i < len(unit) and depth:
            depth += {'{': 1, '}': -1}.get(unit[i], 0)
            i += 1
        # `self.name(` or a bare `name(`, but not `((Base)self).name(` (an ancestor's version, not a recursion).
        if re.search(r'(?<![\w.])(?:self\.)?' + re.escape(name) + r'\s*\(', unit[m.end():i]):
            return True
    return False


# A declared type marks its area wherever it is used, not only where it is declared: a method that
# holds a value-class local or switches on an enum's members combines those areas.
TYPE_DECLS = [
    ('value_class',   r'\bvalue\s+class\s+(\w+)'),
    ('primitive',     r'\bprimitive\s+class\s+(\w+)'),
    ('emitter_class', r'\bemitter\s+class\s+(\w+)'),
    ('pooled',        r'\bclass\s+(\w+)\s*\[\s*\d+\s*\]'),
    ('enums',         r'\b(?:enum|bnum)\s+(\w+)'),
    ('union_var',     r'\bunion\s+(\w+)'),
]


def declared_types(text):
    """area → names of the types this program declares for it."""
    out = {}
    for area, pat in TYPE_DECLS:
        names = {m.group(1).lower() for m in re.finditer(pat, text, re.I)}   # Beguile is case-insensitive
        if names:
            # Only where the name stands as a type: a declaration (`T x`), a type argument or union member
            # (`<T`, `T>`, `T|`), a cast (`(T)x`), a static access (`T.x`), an inline object (`T{`), `new T`.
            # A member that happens to share the name (`self.weight`, `weight = 3`) is not a use.
            alt = '|'.join(re.escape(n) for n in sorted(names))
            out[area] = re.compile(r'(?:(?<=[<|(])\s*|(?<=new\s)|(?<![\w$.]))(?:' + alt + r')\b'
                                   r'(?=\s+(?:ref\s+)?[A-Za-z_]\w*\s*[=;,)(\[{:]|\s*[>|]|\s*\)\s*[\w(]|\.\w|\s*\{|\s*\[)', re.I)
    return out


def areas_in(chunk, types=None):
    found = {k for k in KEYS if any(p.search(chunk) for p in COMPILED[k])}
    for area, pat in (types or {}).items():
        if pat.search(chunk):
            found.add(area)
    if is_recursive(chunk):
        found.add('recursion')
    return found


def scan():
    """pair → list of files that cover it; area → files that use it."""
    pairs, used = {}, {k: set() for k in KEYS}
    global TRIPLES_SEEN, ALL_TRIPLES_SEEN
    TRIPLES_SEEN = {}
    ALL_TRIPLES_SEEN = set()
    for path in corpus():
        text = strip_comments(open(path, encoding='utf-8', errors='replace').read())
        whole = {k for k in FILE_WIDE if any(p.search(text) for p in COMPILED[k])}
        types = declared_types(text)
        rel = os.path.relpath(path, TESTS)
        for u in units(text):
            here = (areas_in(u, types) - FILE_WIDE) | whole
            for k in here:
                used[k].add(rel)
            for a, b in itertools.combinations(sorted(here), 2):
                p = frozenset((a, b))
                if p in IMPOSSIBLE:
                    continue
                pairs.setdefault(p, set()).add(rel)
            for t in itertools.combinations(sorted(here & set(FOCUS)), 3):
                TRIPLES_SEEN.setdefault(frozenset(t), set()).add(rel)
            for t in itertools.combinations(sorted(here), 3):
                ALL_TRIPLES_SEEN.add(frozenset(t))
    return pairs, used


TRIPLES_SEEN = {}
ALL_TRIPLES_SEEN = set()


def all_triple_summary():
    """(accounted for, total) triples of all areas; call after scan()."""
    return sum(1 for t in ALL_AREA_TRIPLES if t in ALL_TRIPLES_SEEN), len(ALL_AREA_TRIPLES)


def all_triple_history():
    if not os.path.exists(ALL_TRIPLE_HISTORY):
        return []
    return list(csv.DictReader(open(ALL_TRIPLE_HISTORY, encoding='utf-8')))


def triple_summary():
    """(covered, total) focus triples; call after scan()."""
    return sum(1 for t in ALL_TRIPLES if t in TRIPLES_SEEN), len(ALL_TRIPLES)


def triple_history():
    if not os.path.exists(TRIPLE_HISTORY):
        return []
    return list(csv.DictReader(open(TRIPLE_HISTORY, encoding='utf-8')))


def summary(pairs):
    covered = sum(1 for p in ALL_PAIRS if p in pairs)
    return covered, len(ALL_PAIRS)


def uncovered_by_area(pairs):
    counts = {k: 0 for k in KEYS}
    for p in ALL_PAIRS:
        if p not in pairs:
            for k in p:
                counts[k] += 1
    return counts


def history():
    if not os.path.exists(HISTORY):
        return []
    return list(csv.DictReader(open(HISTORY, encoding='utf-8')))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--uncovered', nargs='?', const='', metavar='AREA')
    ap.add_argument('--record', metavar='LABEL')
    ap.add_argument('--assign', type=int, metavar='N')
    ap.add_argument('--areas', action='store_true', help='list the areas and how many programs use each')
    ap.add_argument('--as-of-round', type=int, metavar='N', help='count only the style trials written by round N')
    ap.add_argument('--triples', action='store_true', help='with --uncovered / --assign / --record: focus triples, not pairs')
    ap.add_argument('--all-triples', action='store_true', help='with --record / --uncovered: triples of all areas')
    a = ap.parse_args()
    global UP_TO_ROUND
    UP_TO_ROUND = a.as_of_round
    pairs, used = scan()
    covered, total = summary(pairs)
    if a.all_triples:
        ac, at = all_triple_summary()
        if a.uncovered is not None:
            for t in ALL_AREA_TRIPLES:
                if t not in ALL_TRIPLES_SEEN and (not a.uncovered or a.uncovered in t):
                    print(' × '.join(sorted(t)))
            return
        if a.record:
            new = not os.path.exists(ALL_TRIPLE_HISTORY)
            with open(ALL_TRIPLE_HISTORY, 'a', encoding='utf-8', newline='') as f:
                w = csv.writer(f)
                if new:
                    w.writerow(['date', 'label', 'covered', 'total', 'programs'])
                w.writerow([datetime.date.today().isoformat(), a.record, ac, at, len(corpus())])
        print(f'All-area triples accounted for: {ac} of {at} ({100 * ac / at:.1f}%)')
        return
    if a.triples:
        tc, tt = triple_summary()
        gaps = [tuple(sorted(t)) for t in ALL_TRIPLES if t not in TRIPLES_SEEN]
        if a.uncovered is not None:
            for t in gaps:
                if not a.uncovered or a.uncovered in t:
                    print(' × '.join(t))
            return
        if a.assign:
            rarity = {k: len(used[k]) for k in KEYS}
            gaps.sort(key=lambda t: (min(rarity[x] for x in t), t))
            lists = [[] for _ in range(a.assign)]
            for i, t in enumerate(gaps):
                lists[i % a.assign].append(t)
            for i, l in enumerate(lists, 1):
                print(f'== list {i} ({len(l)} triples)')
                for t in l:
                    print('  ' + ' × '.join(t))
            return
        if a.record:
            new = not os.path.exists(TRIPLE_HISTORY)
            with open(TRIPLE_HISTORY, 'a', encoding='utf-8', newline='') as f:
                w = csv.writer(f)
                if new:
                    w.writerow(['date', 'label', 'covered', 'total', 'programs'])
                w.writerow([datetime.date.today().isoformat(), a.record, tc, tt, len(corpus())])
        print(f'Triple coverage: {tc} of {tt} triples ({100 * tc / tt:.1f}%) among {len(FOCUS)} focus areas')
        return
    if a.areas:
        desc = {k: d for k, d, _, _ in AREAS}
        for k in KEYS:
            print(f'{k:15} {len(used[k]):4} programs  {desc[k]}')
        return
    if a.uncovered is not None:
        for p in ALL_PAIRS:
            if p not in pairs and (not a.uncovered or a.uncovered in p):
                print(' × '.join(sorted(p)))
        return
    if a.assign:
        # Deal the gaps round-robin, ordered so each list mixes areas (sorted by the rarer area first).
        gaps = [tuple(sorted(p)) for p in ALL_PAIRS if p not in pairs]
        rarity = {k: len(used[k]) for k in KEYS}
        gaps.sort(key=lambda p: (min(rarity[p[0]], rarity[p[1]]), p))
        lists = [[] for _ in range(a.assign)]
        for i, p in enumerate(gaps):
            lists[i % a.assign].append(p)
        for i, l in enumerate(lists, 1):
            print(f'== list {i} ({len(l)} pairs)')
            for p in l:
                print(f'  {p[0]} × {p[1]}')
        return
    if a.record:
        new = not os.path.exists(HISTORY)
        with open(HISTORY, 'a', encoding='utf-8', newline='') as f:
            w = csv.writer(f)
            if new:
                w.writerow(['date', 'label', 'covered', 'total', 'programs'])
            w.writerow([datetime.date.today().isoformat(), a.record, covered, total, len(corpus())])
    print(f'Interaction coverage: {covered} of {total} area pairs ({100 * covered / total:.1f}%), '
          f'{len(KEYS)} areas, {len(corpus())} programs')
    worst = sorted(uncovered_by_area(pairs).items(), key=lambda kv: -kv[1])[:8]
    print('Most uncovered: ' + ', '.join(f'{k} {n}' for k, n in worst))


if __name__ == '__main__':
    main()
