#!/usr/bin/env python3
"""Mutation pass over the field trials: compile thousands of one-token variants of the valid trials and
check the compiler copes with each.

Each variant of a trial's main .bgl file changes one token: deletes it, duplicates it, swaps it with the
next, or replaces a type name with another. Comments and the #beguilerSettings block are left alone. A
variant must produce a Beguile diagnostic or compile cleanly; these are reported:

  CRASH     beguiler died (signal, abort, uncaught exception)
  HANG      beguiler ran past the time limit
  I6-LEAK   beguiler accepted the program and Inform 6 rejected what it emitted

Usage: mutate_trials.py [--only NAME] [--per-trial N] [--seed S] [--keep DIR] [--jobs J]
  --per-trial  variants per trial (default 40)
  --keep       write each reported variant (and its output) into DIR for triage
Exit status is non-zero when anything is reported.
"""
import argparse, concurrent.futures, glob, itertools, os, random, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
TRIALS = os.path.join(ROOT, 'tests', 'fieldTrials')
BEGUILER = os.path.join(ROOT, 'beguiler')

TOKEN = re.compile(r'''
    (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<string>\$?"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')
  | (?P<word>[A-Za-z_][A-Za-z_0-9]*|\#[A-Za-z_][A-Za-z_0-9]*|\$[A-Za-z_0-9]+)
  | (?P<number>\$?[0-9][0-9A-Fa-f.]*)
  | (?P<op><<=|>>=|<=>|\?\.|::|:=|==|!=|<=|>=|&&|\|\||\+\+|--|\+=|-=|\*=|/=|%=|&=|\|=|\^=|<<|>>|=>|[{}()\[\];,.<>+\-*/%=!&|^~?:])
  | (?P<space>\s+)
''', re.S | re.X)

SERIAL = itertools.count()   # a unique name for each variant file (next() is atomic under the GIL)
TYPES = ['int', 'bool', 'string', 'char', 'object', 'stringObj', 'var', 'void']


def tokens(src):
    """(start, end, kind) for every Beguile code token: not in a comment, the #beguilerSettings block, or
    raw Inform 6 (an #i6 island or an emitter's body), which Beguile passes through unread."""
    out, pos = [], 0
    raw, depth, opener = [], 0, None   # raw: (start, end) spans; opener: where a raw span's `{` is awaited
    pending = None
    while pos < len(src):
        m = TOKEN.match(src, pos)
        if m is None:
            pos += 1
            continue
        kind, text = m.lastgroup, m.group()
        if kind not in ('comment', 'space'):
            if text in ('#i6', '#beguilerSettings', 'emitter', '$i6Expr') and pending is None and depth == 0:
                pending = m.start()
            elif text == '{':
                if pending is not None and depth == 0:
                    opener = pending
                if opener is not None:
                    depth += 1
            elif text == '}' and opener is not None:
                depth -= 1
                if depth == 0:
                    raw.append((opener, m.end()))
                    opener, pending = None, None
            elif text == ';' and opener is None:
                if pending is not None and src[pending:pending + 3] == '#i6':
                    raw.append((pending, m.end()))   # a one-line `#i6 statement;`
                pending = None   # `emitter` on a declaration with no body
            out.append((m.start(), m.end(), kind))
        pos = m.end()
    inside = lambda a: any(r0 <= a < r1 for r0, r1 in raw)
    return [t for t in out if not inside(t[0])]


def mutants(src, n, rng):
    toks = tokens(src)
    if len(toks) < 2:
        return []
    out = []
    for i in range(n):
        k = rng.randrange(len(toks) - 1)
        a, b, kind = toks[k]
        text = src[a:b]
        how = rng.choice(['delete', 'duplicate', 'swap', 'retype'])
        if how == 'retype':
            if text not in TYPES:
                how = 'delete'
            else:
                new = rng.choice([t for t in TYPES if t != text])
                out.append((f'retype {text}->{new} @{a}', src[:a] + new + src[b:]))
                continue
        if how == 'delete':
            out.append((f'delete {text!r} @{a}', src[:a] + src[b:]))
        elif how == 'duplicate':
            out.append((f'duplicate {text!r} @{a}', src[:b] + ' ' + text + src[b:]))
        else:
            c, d, _ = toks[k + 1]
            out.append((f'swap {text!r}/{src[c:d]!r} @{a}', src[:a] + src[c:d] + src[b:c] + text + src[d:]))
    return out


def a_of(label):
    return re.sub(r'\W+', '_', label)[:60]


def compile_variant(path, label, text, keep):
    name = os.path.splitext(os.path.basename(path))[0]
    work = tempfile.mkdtemp(prefix='mut-')
    try:
        # The variant sits beside the trial so its relative includes still resolve.
        vpath = os.path.join(os.path.dirname(path), f'_mut_{os.getpid()}_{next(SERIAL)}_{name}.bgl')
        with open(vpath, 'w', encoding='utf-8') as f:
            f.write(text)
        try:
            p = subprocess.run([BEGUILER, '-o', work, vpath], cwd=os.path.dirname(path), capture_output=True,
                               text=True, errors='replace', timeout=30)
            out, code = p.stdout + p.stderr, p.returncode
        except subprocess.TimeoutExpired:
            out, code = 'TIMEOUT', None
        finally:
            os.remove(vpath)
        status = 'ok'
        if code is None:
            status = 'HANG'
        elif code < 0 or re.search(r'Segmentation fault|Abort trap|terminate called|uncaught exception|std::', out):
            status = 'CRASH'
        elif re.search(r'\.inf:\d+:\d+: ERROR', out) or (not re.search(r': ERROR: ', out)
                and (re.search(r'Compiled with \d+ errors?', out) or 'Error running I6' in out)):
            status = 'I6-LEAK'   # an error in the emitted .inf, translated or not
        if status != 'ok' and keep:
            os.makedirs(keep, exist_ok=True)
            stem = f'{name}_{a_of(label)}'
            with open(os.path.join(keep, stem + '.bgl'), 'w', encoding='utf-8') as f:
                f.write(f'// mutation of {name}: {label}\n' + text)
            with open(os.path.join(keep, stem + '.out'), 'w', encoding='utf-8') as f:
                f.write(out)
        first = next((l for l in out.split('\n') if 'rror' in l), out.strip()[-200:])
        return name, label, status, re.sub(r'^.*?/(?=[^/]*\.(bgl|inf)[:(])', '', first.strip())[:220]
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only')
    ap.add_argument('--per-trial', type=int, default=40)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--keep')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    a = ap.parse_args()
    paths = sorted(p for p in glob.glob(os.path.join(TRIALS, '*.bgl')) if not os.path.basename(p).startswith('_'))
    if a.only:
        paths = [p for p in paths if os.path.splitext(os.path.basename(p))[0] == a.only]
    jobs = []
    for p in paths:
        rng = random.Random(f'{a.seed}:{os.path.basename(p)}')
        src = open(p, encoding='utf-8').read()
        jobs += [(p, label, text) for label, text in mutants(src, a.per_trial, rng)]
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        results = list(ex.map(lambda j: compile_variant(*j, a.keep), jobs))
    counts = {}
    for name, label, status, detail in results:
        counts[status] = counts.get(status, 0) + 1
        if status != 'ok':
            print(f'  {status}: {name} [{label}] — {detail}')
    print('Mutations: ' + ', '.join(f'{v} {k.lower()}' for k, v in sorted(counts.items())))
    sys.exit(0 if all(s == 'ok' for _, _, s, _ in results) else 1)


if __name__ == '__main__':
    main()
