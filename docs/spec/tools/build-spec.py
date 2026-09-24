#!/usr/bin/env python3
"""Merge docs/spec into the single-file reference at docs/languageSpec.md.

Usage:  build-spec.py [--check]
  (default)   rewrite docs/languageSpec.md from the chapter files
  --check     compare only; exit 1 when the merged file is out of date

The chapter order and the part headings come from README.md's <!-- toc --> block, so the
merged file cannot disagree with the chapter index about what the spec contains.

Links between chapters (`03-declarations-scope.md#310-shadowing`) become in-document
anchors (`#310-shadowing`). Every heading in the spec is numbered, so the 422 headings
produce 422 distinct slugs and no anchor needs disambiguating; the script checks that and
fails rather than emitting a file with two headings competing for one anchor.
"""
import importlib.util, pathlib, re, sys

SPEC = pathlib.Path(__file__).resolve().parent.parent
OUT  = SPEC.parent / 'languageSpec.md'

# slug() is the anchor rule the per-file TOCs already use; share it rather than restating it.
_spec = importlib.util.spec_from_file_location('build_toc', SPEC / 'tools' / 'build-toc.py')
_toc = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(_toc)
slug = _toc.slug

TOC_RE     = re.compile(r'<!-- toc -->.*?<!-- /toc -->\n?', re.S)
H_RE       = re.compile(r'^(#{1,6})\s+(.*?)\s*$')
TOP_ENTRY  = re.compile(r'^- \[(?P<title>[^\]]+)\]\((?P<file>[0-9A-J][^)#]*\.md)\)\s*$')
PART_RE    = re.compile(r'^\*\*(?P<label>[^*]+)\*\*\s*$')
LINK_RE    = re.compile(r'\]\((?P<file>[0-9A-J][A-Za-z0-9_.-]*\.md)(?P<frag>#[^)]*)?\)')

BANNER = """<!-- GENERATED FILE — do not edit.
     Built from docs/spec/ by docs/spec/tools/build-spec.py.
     Edit the chapter there and re-run the script; edits here are lost on the next build. -->

"""


def read_index():
    """The chapter order and part labels, from README's TOC. Returns [(part|None, file)]."""
    readme = (SPEC / 'README.md').read_text()
    block = TOC_RE.search(readme)
    if not block:
        sys.exit('README.md has no <!-- toc --> block to take the chapter order from.')
    order, part = [], None
    for line in block.group(0).split('\n'):
        p = PART_RE.match(line)
        if p:
            part = p.group('label').strip()
            continue
        e = TOP_ENTRY.match(line)
        if e:
            order.append((part, e.group('file')))
            part = None   # the label belongs to the first chapter under it
    return order


def heading_anchor(path, text):
    """The anchor a chapter's own title gets, for rewriting a bare `file.md` link."""
    for line in text.split('\n'):
        m = H_RE.match(line)
        if m:
            return slug(m.group(2))
    sys.exit(f'{path.name} has no heading to anchor a bare link to.')


def build():
    order = read_index()
    bodies, titles, problems = [], {}, []

    for part, fname in order:
        path = SPEC / fname
        if not path.exists():
            problems.append(f'README lists {fname}, which does not exist')
            continue
        # Removing the chapter's own <!-- toc --> leaves a gap where it stood; close it so the
        # merged file reads as one document rather than a pile of stripped ones.
        text = re.sub(r'\n{3,}', '\n\n', TOC_RE.sub('', path.read_text())).strip('\n')
        titles[fname] = heading_anchor(path, text)
        bodies.append((part, fname, text))

    known = set(titles)
    for _, fname, text in bodies:
        for m in LINK_RE.finditer(text):
            if m.group('file') not in known:
                problems.append(f'{fname}: link to {m.group("file")}, which the merged file does not contain')

    # Anchor uniqueness: the merged file has one namespace, where the chapters had many.
    seen, in_code = {}, False
    for _, fname, text in bodies:
        for line in text.split('\n'):
            if line.startswith('```'):
                in_code = not in_code
                continue
            if in_code:
                continue
            m = H_RE.match(line)
            if not m:
                continue
            a = slug(m.group(2))
            if a in seen:
                problems.append(f'{fname}: heading "{m.group(2)}" collides with {seen[a]} on anchor #{a}')
            else:
                seen[a] = fname

    if problems:
        for p in problems:
            print('  ' + p, file=sys.stderr)
        sys.exit(f'{len(problems)} problem(s); languageSpec.md not written.')

    def relink(text):
        def sub(m):
            frag = m.group('frag')
            return '](' + (frag if frag else '#' + titles[m.group('file')]) + ')'
        return LINK_RE.sub(sub, text)

    # README's own TOC is already section-deep and already maintained by build-toc.py; relinked, it
    # is exactly the index an 11,000-line single file needs. Take it rather than build a shallower one.
    readme = (SPEC / 'README.md').read_text()
    block = TOC_RE.search(readme).group(0)
    inner = block.replace('<!-- toc -->', '').replace('<!-- /toc -->', '').strip('\n')
    out = [BANNER + relink(TOC_RE.sub('', readme).strip('\n')), '', '## Contents', '', relink(inner)]
    for part, fname, text in bodies:
        out.append('\n---\n')
        if part:
            out.append(f'# {part}\n')
        out.append(relink(text))
    return '\n'.join(out).rstrip('\n') + '\n'


if __name__ == '__main__':
    merged = build()
    if '--check' in sys.argv:
        current = OUT.read_text() if OUT.exists() else ''
        if current != merged:
            sys.exit(f'{OUT.name} is out of date — re-run {pathlib.Path(__file__).name}.')
        print(f'{OUT.name} is up to date ({len(merged.splitlines())} lines).')
    else:
        OUT.write_text(merged)
        print(f'wrote {OUT.relative_to(SPEC.parent.parent)} ({len(merged.splitlines())} lines).')
