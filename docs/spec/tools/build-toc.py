#!/usr/bin/env python3
"""Regenerate tables of contents for docs/spec and validate headings and cross-references.

Usage:  build-toc.py [--check] [--resolve] [--audit]
  (default)   rewrite each chapter's <!-- toc --> block and README's, then validate
  --check     validate only; exit 1 on any problem
  --audit     list every cross-chapter §N.M reference with its target heading and context, for review
  --resolve   also rewrite cross-chapter refs of the form `§N *Heading words*` to `§N.M[.K]`
              when the italic title matches exactly one heading in chapter N
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
CHAPTER_RE = re.compile(r'^(\d{2}|[A-J])-[a-z0-9-]+\.md$')
H_RE = re.compile(r'^(#{1,6})\s+(.*?)\s*$')
NUM_RE = re.compile(r'^(?:(\d+)(?:\.(\d+))?(?:\.(\d+))?|Appendix ([A-J])(?:\s+|$)|([A-J])\.(\d+)(?:\.(\d+))?)\s*(.*)$')
REF_RE = re.compile(r'§(\d+|[A-J])(?:\.(\d+))?(?:\.(\d+))?(?:\s+\*([^*\n]+)\*)?')

def slug(text):
    text = re.sub(r'[`*_]', '', text).strip().lower()
    text = re.sub(r'[^\w\s-]', '', text)
    return re.sub(r'[\s]+', '-', text)

def chapter_id(path):
    m = CHAPTER_RE.match(path.name)
    if not m: return None
    cid = m.group(1)
    return None if cid == '00' else (str(int(cid)) if cid.isdigit() else cid)

KNOWN_CHAPTERS = {str(n) for n in range(1, 24)} | set('ABCDEFGHIJ')

def skip_mask(lines):
    """True for lines inside fenced code or HTML comments (not scanned for headings/refs)."""
    mask, in_code, in_comment = [], False, False
    for l in lines:
        if l.startswith('```'): in_code = not in_code; mask.append(True); continue
        if not in_code and '<!--' in l and '-->' not in l: in_comment = True
        skip = in_code or in_comment or ('<!--' in l)
        if in_comment and '-->' in l: in_comment = False
        mask.append(skip)
    return mask

def parse(path):
    """Return (lines, headings) where headings = [(line_no, level, number_tuple, title, raw)]."""
    lines = path.read_text(encoding='utf-8').split('\n')
    heads = []
    mask = skip_mask(lines)
    for i, l in enumerate(lines):
        if mask[i]: continue
        m = H_RE.match(l)
        if not m: continue
        level = len(m.group(1)); raw = m.group(2)
        n = NUM_RE.match(raw)
        num, title = None, raw
        if n:
            if n.group(1):
                num = tuple(x for x in (n.group(1), n.group(2), n.group(3)) if x)
            elif n.group(4):
                num = (n.group(4),)
            elif n.group(5):
                num = tuple(x for x in (n.group(5), n.group(6), n.group(7)) if x)
            title = n.group(8)
        heads.append((i, level, num, title, raw))
    return lines, heads

def validate(path, cid, heads, problems):
    top = [h for h in heads if h[1] == 1]
    if len(top) != 1:
        problems.append(f'{path.name}: expected exactly one "#" heading, found {len(top)}')
    if cid is None:  # front matter: plain ## allowed, depth ≤ 2
        for i, level, num, title, raw in heads:
            if level > 3: problems.append(f'{path.name}:{i+1}: heading deeper than ### ({raw})')
        return
    prev = None
    for i, level, num, title, raw in heads:
        if level > 3:
            problems.append(f'{path.name}:{i+1}: heading deeper than ### ({raw})'); continue
        if num is None:
            problems.append(f'{path.name}:{i+1}: unnumbered heading ({raw})'); continue
        if num[0] != cid:
            problems.append(f'{path.name}:{i+1}: heading numbered for chapter {num[0]}, file is chapter {cid} ({raw})')
        if len(num) != level:
            problems.append(f'{path.name}:{i+1}: heading level {level} does not match number depth {len(num)} ({raw})')
        if prev and level > 1:
            # sequence check within parent
            if len(num) == len(prev) and num[:-1] == prev[:-1] and int(num[-1]) != int(prev[-1]) + 1:
                problems.append(f'{path.name}:{i+1}: numbering jumps {".".join(prev)} → {".".join(num)}')
            if len(num) == len(prev) + 1 and num[:-1] == prev and num[-1] != '1':
                problems.append(f'{path.name}:{i+1}: first child should be .1 ({raw})')
        prev = num

def toc_block(heads):
    out = ['<!-- toc -->']
    if sum(1 for h in heads if 1 < h[1] <= 3) < 3:
        return out + ['<!-- /toc -->']
    for i, level, num, title, raw in heads:
        if level == 1 or level > 3: continue
        indent = '  ' * (level - 2)
        label = ('.'.join(num) + ' ' if num else '') + title
        out.append(f'{indent}- [{label}](#{slug(raw)})')
    out.append('<!-- /toc -->')
    return out

def splice_toc(lines, block):
    try:
        a = lines.index('<!-- toc -->'); b = lines.index('<!-- /toc -->')
        if block == ['<!-- toc -->', '<!-- /toc -->']:
            # nothing to list: drop the markers and any blank line that followed them
            rest = lines[b+1:]
            while rest and rest[0] == '': rest = rest[1:]
            head = lines[:a]
            while head and head[-1] == '': head = head[:-1]
            return head + [''] + rest
        return lines[:a] + block + lines[b+1:]
    except ValueError:
        if block == ['<!-- toc -->', '<!-- /toc -->']:
            return lines
        # insert after the first '#' heading
        for i, l in enumerate(lines):
            if l.startswith('# '):
                return lines[:i+1] + [''] + block + [''] + lines[i+1:]
        return lines

def main():
    check = '--check' in sys.argv; resolve = '--resolve' in sys.argv
    files = sorted((p for p in ROOT.glob('*.md') if CHAPTER_RE.match(p.name)),
                   key=lambda p: p.name)
    problems, index = [], {}   # index[cid] = {slug_title: number_string}
    parsed = {}
    for p in files:
        cid = chapter_id(p); lines, heads = parse(p); parsed[p] = (cid, lines, heads)
        validate(p, cid, heads, problems)
        if cid:
            index[cid] = {}
            for i, level, num, title, raw in heads:
                if num and len(num) > 1:
                    index[cid].setdefault(title.strip().lower(), []).append('.'.join(num))
                    index[cid].setdefault(re.sub(r'[`*]', '', title).strip().lower(), []).append('.'.join(num))
    # cross-references
    for p, (cid, lines, heads) in parsed.items():
        own = {'.'.join(h[2]) for h in heads if h[2]}
        changed = False; mask = skip_mask(lines)
        for li, l in enumerate(lines):
            if mask[li]: continue
            def repl(m):
                nonlocal changed
                ch, s, k, title = m.group(1), m.group(2), m.group(3), m.group(4)
                if ch not in KNOWN_CHAPTERS:
                    problems.append(f'{p.name}:{li+1}: §{ch} is not a chapter'); return m.group(0)
                exists = ch in index or ch == cid
                if s:
                    full = '.'.join(x for x in (ch, s, k) if x)
                    if not exists:
                        return m.group(0)  # chapter not written yet; checked later
                    target_set = own if ch == cid else {v for vs in index.get(ch, {}).values() for v in vs}
                    if full not in target_set:
                        problems.append(f'{p.name}:{li+1}: §{full} does not resolve to a heading')
                    return m.group(0)
                if title and ch != cid and exists:
                    hits = index.get(ch, {}).get(re.sub(r'[`*]', '', title).strip().lower(), [])
                    if len(hits) == 1 and resolve:
                        changed = True; return f'§{hits[0]}'
                    if len(hits) == 0:
                        problems.append(f'{p.name}:{li+1}: §{ch} *{title}* matches no heading in chapter {ch}')
                return m.group(0)
            new = REF_RE.sub(repl, l)
            if new != l: lines[li] = new
        if changed and not check:
            p.write_text('\n'.join(lines), encoding='utf-8')
    # TOCs
    if not check:
        for p, (cid, lines, heads) in parsed.items():
            lines = splice_toc(lines, toc_block(heads))
            p.write_text('\n'.join(lines), encoding='utf-8')
        readme = ROOT / 'README.md'
        rl = readme.read_text(encoding='utf-8').split('\n')
        block = ['<!-- toc -->']
        PARTS = [(None, '**Front matter**'), ('1', '**Part I — The Beguile Language**'), ('16', '**Part II — The Beguiler Compiler**'),
                 ('21', '**Part III — The Beguile Language Runtime (BLR)**'), ('A', '**Appendices**')]
        shown = set()
        for p, (cid, lines, heads) in parsed.items():
            top = next((h for h in heads if h[1] == 1), None)
            if not top: continue
            for key, label in PARTS:
                if label in shown: continue
                if (key is None and cid is None) or (key is not None and cid == key):
                    block.append(''); block.append(label); shown.add(label)
            block.append(f'- [{top[4]}]({p.name})')
            for i, level, num, title, raw in heads:
                if level in (2, 3):
                    label = ('.'.join(num) + ' ' if num else '') + title
                    block.append(f'{"  " * (level - 1)}- [{label}]({p.name}#{slug(raw)})')
        block.append('<!-- /toc -->')
        readme.write_text('\n'.join(splice_toc(rl, block)), encoding='utf-8')
    if '--audit' in sys.argv:
        titles = {}
        for p, (cid, lines, heads) in parsed.items():
            for i, level, num, title, raw in heads:
                if num: titles['.'.join(num)] = title
        for p, (cid, lines, heads) in parsed.items():
            mask = skip_mask(lines)
            for li, l in enumerate(lines):
                if mask[li]: continue
                for m in REF_RE.finditer(l):
                    ch, s_, k = m.group(1), m.group(2), m.group(3)
                    if not s_ or ch == cid: continue
                    full = '.'.join(x for x in (ch, s_, k) if x)
                    ctx = l[max(0, m.start()-60):m.end()+30].replace('|', ' ')
                    print(f'{p.name}:{li+1}: §{full} = "{titles.get(full, "?")}"   …{ctx.strip()}…')
        sys.exit(0)
    for pr in problems: print(pr)
    print(f'{len(files)} files, {len(problems)} problems')
    sys.exit(1 if problems else 0)

if __name__ == '__main__':
    main()
