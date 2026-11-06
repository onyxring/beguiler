#!/usr/bin/env python3
"""Checks a #mergeI6 patch of the Inform 6 library against the stock library.

check  Compiles a game against the library and against the library with patches applied, for the
       Z-machine and Glulx, runs both on the same commands and compares the transcripts. With no extension
       hooked in, a patch that only adds hooks changes nothing, so every difference is a behavior change
       the patch makes.

         i6patch.py check --library <dir> --game <game.inf> --commands <file> [--decls <file.h>]
                          [--decls-from <binding.bgl>] <patch> …

       The game includes "decls" when PATCHED is defined: what the patch's code needs declared (its
       properties and globals). --decls-from takes them from a binding's #emitfirst blocks, leaving out its
       `_STAGE` constants.

Exits non-zero on a difference.
"""
import argparse, difflib, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
BEGUILER = os.path.join(ROOT, 'beguiler')
INFORM6 = os.environ.get('INFORM6') or os.path.join(ROOT, '..', 'inform6', 'inform6')
ZVM = os.environ.get('ZVM') or os.path.join(ROOT, '..', 'beguilex', 'node_modules', '.bin', 'zvm')
GLULX_RUN = os.path.join(HERE, 'glulx-run.js')


def decls_from(bgl):
    text = open(bgl, encoding='latin-1').read()
    out = []
    for block in re.findall(r'#emitfirst\s*\{(.*?)\n\}', text, re.S):
        out += [l for l in block.split('\n') if not re.search(r'Constant\s+\w+_STAGE\b', l)]
    return '\n'.join(out) + '\n'


def run_story(story, glulx, commands):
    cmd = ['node', GLULX_RUN, story] if glulx else [ZVM, story]
    r = subprocess.run(cmd, input=commands, capture_output=True, text=True, timeout=120)
    return r.stdout + r.stderr


def check(args):
    commands = open(args.commands).read()
    # Inform 6 doesn't find includes in a directory whose name ends in '_', which a random name can.
    with tempfile.TemporaryDirectory(suffix='.d') as d:
        patched = os.path.join(d, 'patched'); os.makedirs(patched)
        game_dir = os.path.dirname(os.path.abspath(args.game))
        text = ''
        for b in args.decls_from or []: text += decls_from(b)
        for f in args.decls or []: text += open(f).read()
        open(os.path.join(d, 'decls.h'), 'w').write(text)
        for patch in args.patches:
            for n in re.findall(r'^\+\+\+ (?:b/)?([^\t\n]+)', open(patch).read(), re.M):
                name = os.path.basename(n.strip())
                done = os.path.join(patched, name)
                src = done if os.path.exists(done) else os.path.join(args.library, name)
                tmp = os.path.join(d, name)
                r = subprocess.run([BEGUILER, '--apply-patch', patch, src, '-o', tmp], capture_output=True, text=True)
                if r.returncode != 0 or '"failed": []' not in r.stdout:
                    print(f'{os.path.basename(patch)} does not apply to {name}: {r.stdout}{r.stderr}'); return 1
                shutil.move(tmp, done)
        problems = 0
        for glulx in (False, True):
            target = 'Glulx' if glulx else 'Z-machine'
            outs = {}
            for variant, paths, define in (('stock', [game_dir, args.library], []),
                                           ('patched', [patched, d, game_dir, args.library], ['$#PATCHED=1'])):
                story = os.path.join(d, f'{variant}.{"ulx" if glulx else "z5"}')
                cmd = [INFORM6, '-G' if glulx else '-v5', *define, '+include_path=' + ','.join(paths), args.game, story]
                r = subprocess.run(cmd, capture_output=True, text=True)
                if not os.path.exists(story) or re.search(r'Compiled with \d+ errors?', r.stdout):
                    print(f'  FAIL {target} {variant}: Inform 6 failed'); print(r.stdout[-2000:]); return 1
                outs[variant] = run_story(story, glulx, commands)
            diff = list(difflib.unified_diff(outs['stock'].splitlines(), outs['patched'].splitlines(),
                                             'stock', 'patched', lineterm='', n=1))
            if diff:
                problems += 1
                print(f'  FAIL {target}: the patched library differs from the stock library'); print('\n'.join(diff[:80]))
            else:
                print(f'  ok   {target}: the patched library matches the stock library')
        return 1 if problems else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('check'); c.add_argument('--library', required=True); c.add_argument('--game', required=True)
    c.add_argument('--commands', required=True); c.add_argument('--decls', action='append')
    c.add_argument('--decls-from', action='append'); c.add_argument('patches', nargs='+')
    args = ap.parse_args()
    sys.exit(check(args))


if __name__ == '__main__':
    main()
