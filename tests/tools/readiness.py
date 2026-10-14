#!/usr/bin/env python3
"""Readiness report from the defect ledger: is the compiler approaching stability?

Inputs (tests/defects/):
  ledger.csv   one row per defect: id, found_date, fixed_date (empty = open), area, found_by, kind,
               severity, summary, evidence, confidence
  probes.csv   one row per probe round: date, found_by, units, unit, match (an evidence tag that picks
               the round's defects when several share a day, or an ID range such as D1438-D1449), note

found_by sorts each defect into how it surfaced:
  development                      found while building something (the code being written exposed it)
  doc-tests / feature-matrix /
  field-trial / test-suite         found by a deliberate probe of existing behavior
  jim / wip-game                   escapes: reached an author before any test caught it

Signals, strongest first:
  probe yield    defects per probe unit, per round. A falling yield on fresh probes is the stability signal.
  latent share   of defects found each month, how many were latent (old code) rather than regressions.
  escapes        defects that reached an author.
  per 1k lines   defects found per thousand lines of compiler + BLR change that month (git, plus the
                 uncommitted working tree for the current month). Commits are batched, so read it monthly.

Headline: defects found per 1,000 lines of new work, over the last 4 active weeks, with a verdict:
  STABLE    under 1 per 1k lines, no escapes, and a probe round run since the last code change (quiet
            without probing proves nothing)
  SETTLING  under 5 per 1k lines
  FRAGILE   more than that
New work is the net lines (added less removed, per commit, so a refactor's moved code cancels) added to the compiler (*.cpp, *.h), the BLR, the field trials, the examples and the
WIP games: the code whose writing exposes defects, old or new. Not counted: regression tests and baselines
(written after a defect is found), generated output, and commits whose message names a ledger ID (Dnnn) —
those are fixes, and counting them would make a week of fixing look like a stable week. Uncommitted
compiler and BLR changes can't yet be told apart from fixes, so they count once committed; uncommitted
trials, examples and games count now, dated by when the file was last edited.
An active week is one with new work, a defect found, or a probe round. Idle weeks are skipped: a break
leaves the verdict where the work left it rather than making the compiler look stable.
Secondary: regressions per 1k lines of compiler + BLR change — whether new compiler work is clean.

Rows without a found_date are counted in totals but not in the monthly table.

Usage: readiness.py [--by area|severity|found_by] [--open] [--markdown]
"""
import argparse, csv, datetime, os, re, subprocess
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
TRIALS_DIR = os.path.join(ROOT, 'tests', 'fieldTrials')
DEFECTS = os.path.join(ROOT, 'tests', 'defects')
PROBES = {'doc-tests', 'feature-matrix', 'field-trial', 'test-suite'}
ESCAPES = {'jim', 'wip-game'}
# Paths are anchored at the repo root (`:(glob)`): a plain `*.cpp` also matches any subfolder, such as an
# old `backup/` copy of the sources.
CODE = [':(glob)*.cpp', ':(glob)*.h', ':(glob)beguiLib/**']
WORK = [':(glob)*.cpp', ':(glob)*.h', ':(glob)beguiLib/**/*.bgl', ':(glob)tests/fieldTrials/**/*.bgl',
        ':(glob)examples/**/*.bgl']
WORK_NOW = [':(glob)tests/fieldTrials/**/*.bgl', ':(glob)examples/**/*.bgl']   # uncommitted work counted before its commit
PARENT = os.path.dirname(ROOT)                               # IF-Projects, which holds the WIP games
GAMES = ['WIP/*.bgl']


def beguile_start():
    """The day Beguile began: the first commit of its library. The repository is older (it holds an
    earlier prototype), and that history isn't Beguile's."""
    out = subprocess.run(['git', 'log', '--reverse', '--format=%ad', '--date=short', '--', 'beguiLib'],
                         cwd=ROOT, capture_output=True, text=True).stdout.split()
    return out[0] if out else '2026-03-01'


START = beguile_start()


def source(found_by):
    return 'probe' if found_by in PROBES else 'escape' if found_by in ESCAPES else 'dev'


def week_of(d):
    """Monday of the ISO week containing date string d."""
    day = datetime.date.fromisoformat(d)
    return (day - datetime.timedelta(days=day.weekday())).isoformat()


def churn_by(key):
    """Lines of compiler + BLR change per period (key maps a commit date to its period); the uncommitted
    working tree counts toward the current period."""
    out = subprocess.run(['git', 'log', '--numstat', '--format=C %ad', '--date=short', '--since=' + START, '--'] + CODE,
                         cwd=ROOT, capture_output=True, text=True).stdout
    lines, period = Counter(), None
    for row in out.split('\n'):
        if row.startswith('C '):
            period = key(row[2:12])
        else:
            f = row.split('\t')
            if len(f) == 3 and f[0] != '-':
                lines[period] += int(f[0]) + int(f[1])
    # Uncommitted work belongs to when it was last edited, not to whenever the report runs.
    dirty, edited = 0, None
    diff = subprocess.run(['git', 'diff', '--numstat', 'HEAD', '--'] + CODE,
                          cwd=ROOT, capture_output=True, text=True).stdout
    for row in diff.split('\n'):
        f = row.split('\t')
        if len(f) == 3 and f[0] != '-':
            dirty += int(f[0]) + int(f[1])
            path = os.path.join(ROOT, f[2])
            if os.path.exists(path):
                day = datetime.date.fromtimestamp(os.path.getmtime(path)).isoformat()
                edited = max(edited or day, day)
    if dirty:
        lines[key(edited or datetime.date.today().isoformat())] += dirty
    return lines


def git(repo, *args):
    return subprocess.run(['git'] + list(args), cwd=repo, capture_output=True, text=True).stdout


def new_lines_by_week():
    """Lines of new work added per week (see the header): commits not naming a ledger ID, plus
    uncommitted trials, examples and games dated by their last edit."""
    lines = Counter()
    for repo, paths, now in ((ROOT, WORK, WORK_NOW), (PARENT, GAMES, GAMES)):
        if not os.path.isdir(os.path.join(repo, '.git')) and not os.path.isfile(os.path.join(repo, '.git')):
            continue
        fixes = set(git(repo, 'log', '--format=%H', '-E', r'--grep=\bD[0-9]{3,}\b').split())
        # A commit's new code is its insertions less its deletions: code moved by a refactor cancels.
        week, skip, added, removed = None, False, 0, 0
        def close():
            if week and not skip:
                lines[week] += max(0, added - removed)
        for row in git(repo, 'log', '--numstat', '--format=C %H %ad', '--date=short', '--since=' + START, '--', *paths).split('\n'):
            if row.startswith('C '):
                close()
                _, sha, day = row.split(' ')
                week, skip, added, removed = week_of(day), sha in fixes, 0, 0
                continue
            f = row.split('\t')
            if len(f) == 3 and f[0] != '-' and '/output/' not in f[2]:
                added += int(f[0]); removed += int(f[1])
        close()
        dated = []
        for row in git(repo, 'diff', '--numstat', 'HEAD', '--', *now).split('\n'):
            f = row.split('\t')
            if len(f) == 3 and f[0] != '-' and '/output/' not in f[2]:
                dated.append((f[2], max(0, int(f[0]) - int(f[1]))))
        for path in git(repo, 'ls-files', '--others', '--exclude-standard', '--', *now).split('\n'):
            if path and '/output/' not in path:
                try:
                    with open(os.path.join(repo, path), encoding='utf-8', errors='replace') as fh:
                        dated.append((path, sum(1 for _ in fh)))
                except OSError:
                    pass
        for path, n in dated:
            full = os.path.join(repo, path)
            if os.path.exists(full):
                lines[week_of(datetime.date.fromtimestamp(os.path.getmtime(full)).isoformat())] += n
    return lines


def last_change():
    """Date of the newest compiler or BLR change: the newest commit, or a later uncommitted edit."""
    days = [d for d, n in churn_by(lambda d: d).items() if n and d]
    return max(days) if days else ''


def table(headers, rows, md):
    if md:
        out = ['| ' + ' | '.join(headers) + ' |', '|' + '---|' * len(headers)]
        out += ['| ' + ' | '.join(str(c) for c in r) + ' |' for r in rows]
    else:
        w = [max(len(str(x)) for x in col) for col in zip(headers, *rows)]
        out = ['  '.join(str(c).rjust(n) for c, n in zip(r, w)) for r in [headers] + rows]
    return '\n'.join(out)


def period_rows(rows, periods, key, churn):
    found = defaultdict(list)
    for r in rows:
        if r['found_date']:
            found[key(r['found_date'])].append(r)
    out = []
    for p in periods:
        rs = found.get(p, [])
        src = Counter(source(r['found_by']) for r in rs)
        fixed = sum(1 for r in rows if r['fixed_date'] and key(r['fixed_date']) == p)
        per1k = f"{1000 * len(rs) / churn[p]:.1f}" if churn[p] else '-'
        out.append([p, len(rs), src['dev'], src['probe'], src['escape'], fixed,
                    sum(1 for r in rs if r['kind'] == 'latent'),
                    sum(1 for r in rs if r['severity'] == 'wrong-runtime'), churn[p], per1k])
    return out


STABLE_RATE, SETTLING_RATE, WINDOW = 1.0, 5.0, 4
SPARK = ' ▁▂▃▄▅▆▇█'


def active_weeks(rows, probes, first, work):
    """Weeks from `first` with new work, a compiler or BLR change, a defect found, or a probe round."""
    weeks = {w for w, n in churn_by(week_of).items() if n and w}
    weeks |= {w for w, n in work.items() if n and w}
    weeks |= {week_of(r['found_date']) for r in rows if r['found_date']}
    weeks |= {week_of(p['date']) for p in probes}
    return sorted(w for w in weeks if first <= w <= week_of(datetime.date.today().isoformat()))


def weekly_rates(rows, weeks, work):
    """[(week, defects found, lines of new work, found per 1k lines over it and the 3 active weeks before)]."""
    found = Counter(week_of(r['found_date']) for r in rows if r['found_date'])
    out = []
    for i, w in enumerate(weeks):
        span = weeks[max(0, i - WINDOW + 1):i + 1]
        n, k = sum(found[x] for x in span), sum(work[x] for x in span)
        out.append((w, found[w], work[w], 1000 * n / k if k else None))
    return out


def headline(rows, probes, rates):
    rate = rates[-1][3] if rates[-1][3] is not None else float('inf')
    prev = rates[-1 - WINDOW][3] if len(rates) > WINDOW and rates[-1 - WINDOW][3] is not None else None
    window = {r[0] for r in rates[-WINDOW:]}
    found = sum(r[1] for r in rates[-WINDOW:])
    work = sum(r[2] for r in rates[-WINDOW:])
    churn = churn_by(week_of)
    regressions = sum(1 for r in rows if r['found_date'] and week_of(r['found_date']) in window
                      and r['kind'] == 'regression')
    changed_lines = sum(churn[w] for w in window)
    escapes = sum(1 for r in rows if r['found_date'] and week_of(r['found_date']) in window
                  and source(r['found_by']) == 'escape')
    changed = last_change()
    probed = any(p['date'] >= changed for p in probes)
    if rate <= STABLE_RATE and not escapes and probed:
        verdict = 'STABLE'
    elif rate <= SETTLING_RATE:
        verdict = 'SETTLING'
    else:
        verdict = 'FRAGILE'
    trend = ('' if prev is None else
             f", {'↓ improving' if rate < prev else '↑ worsening' if rate > prev else '→ flat'} from {prev:.1f}")
    idle = (datetime.date.today() - datetime.date.fromisoformat(rates[-1][0])).days // 7
    shown = f"{rate:.1f}" if rate != float('inf') else "∞"
    reg = f"{1000 * regressions / changed_lines:.2f}" if changed_lines else '-'
    return (f"**Stability: {verdict}** — {shown} defects found per 1,000 lines of new work (last {WINDOW} active "
            f"weeks: {found} found in {work:,} lines){trend}. Escapes in those weeks: {escapes}. Probe round since "
            f"the last code change ({changed}): {'yes' if probed else 'no'}. "
            + (f"No activity for {idle} weeks; the verdict stands where the work left it. " if idle >= 1 else "")
            + f"Target for STABLE: under {STABLE_RATE:g} per 1k lines, 0 escapes, and a probe round after the last "
            f"change.\n\nRegressions per 1,000 lines of compiler + BLR change: {reg} ({regressions} in "
            f"{changed_lines:,} lines).")


def sparkline(values):
    top = max(values) or 1
    return ''.join(SPARK[round(v / top * (len(SPARK) - 1))] for v in values)


HEAD = ['found', 'dev', 'probe', 'escape', 'fixed', 'latent', 'wrong-runtime', 'lines changed', 'per 1k lines']


def report(rows, probes, by, md):
    h = (lambda t: f'\n## {t}\n') if md else (lambda t: f'\n{t}:')
    dated = [r['found_date'] for r in rows if r['found_date']]
    out = [f"# Beguile readiness — {datetime.date.today().isoformat()}\n" if md else '',
           f"Defects: {len(rows)} ({sum(1 for r in rows if not r['fixed_date'])} open, "
           f"{sum(1 for r in rows if not r['found_date'])} undated). Newest ledger entry: {max(dated)}."]

    try:   # the interaction coverage, at the top beside the defect count (detail in its own section)
        import interaction_map as _im
        _c, _t = _im.summary(_im.scan()[0])
        out[-1] += f" Interaction coverage: {_c} of {_t} feature pairs ({100 * _c / _t:.1f}%)."
    except Exception:
        pass

    first = week_of(min(r['found_date'] for r in rows if r['found_date'] and r['found_date'] >= '2026-03-01'))
    work = new_lines_by_week()
    rates = weekly_rates(rows, active_weeks(rows, probes, first, work), work)
    out.insert(1, headline(rows, probes, rates) + '\n')
    out.append(h('Trend — defects found per 1,000 lines of new work, over 4 active weeks'))
    vals = [r[3] or 0 for r in rates]
    out.append(('`' if md else '') + sparkline(vals) + ('`' if md else '')
               + f"  {rates[0][0]} → {rates[-1][0]}, peak {max(vals):.1f}, now {vals[-1]:.1f}")
    out.append('')
    # fixed: defects fixed since the previous active week (a fix lands in an active week, a quiet week
    # holds none); open: defects found by the end of the week and not yet fixed by then.
    week_end = lambda w: (datetime.date.fromisoformat(w) + datetime.timedelta(days=6)).isoformat()
    shown = rates[-16:]
    prev_end = {w: week_end(rates[i - 1][0]) if i > 0 else '' for i, (w, *_) in enumerate(rates)}
    def fixed_in(w):
        return sum(1 for r in rows if r['fixed_date'] and prev_end[w] < r['fixed_date'] <= week_end(w))
    def open_at(w):
        e = week_end(w)
        return sum(1 for r in rows if r['found_date'] and r['found_date'] <= e
                   and not (r['fixed_date'] and r['fixed_date'] <= e))
    out.append(table(['active week', 'found', 'fixed', 'open', 'new lines', 'per 1k (week)', 'per 1k (4 wk)', ''],
                     [[w, n, fixed_in(w), open_at(w), k, f'{1000 * n / k:.1f}' if k else '-',
                       '-' if a is None else f'{a:.1f}', '█' * min(60, round(a or 0))]
                      for w, n, k, a in shown], md))
    out.append(h('Probe yield (falling on fresh probes is the strongest stability signal)'))
    prow = []
    for i, p in enumerate(probes):
        rng = re.fullmatch(r'D(\d+)-D(\d+)', p.get('match') or '')
        if rng:              # a round that names its defects by ledger ID range
            lo, hi = int(rng.group(1)), int(rng.group(2))
            n = sum(1 for r in rows if r['id'][1:].isdigit() and lo <= int(r['id'][1:]) <= hi
                    and 'WITHDRAWN' not in r['summary'])
        elif p.get('match'):   # rounds on one day are told apart by their evidence tag
            n = sum(1 for r in rows if r['found_by'] == p['found_by'] and p['match'] in r['evidence'])
        else:
            later = [q['date'] for q in probes[i + 1:] if q['found_by'] == p['found_by']]
            end = later[0] if later else '9999'
            n = sum(1 for r in rows if r['found_by'] == p['found_by'] and p['date'] <= r['found_date'] < end)
        # A round whose note names its trials: its own rate per 1,000 lines of those programs.
        lines, named = 0, 0
        for name in set(re.findall(r'\b([a-z][a-z0-9_]*_\d+)\b', p.get('note') or '')):
            path = os.path.join(TRIALS_DIR, name + '.bgl')
            if os.path.exists(path):
                named += 1
                with open(path, encoding='utf-8', errors='replace') as f: lines += sum(1 for _ in f)
        if p['unit'] != 'trial' or named != int(p['units']): lines = 0   # only when every trial of the run is named
        prow.append([p['date'], p['found_by'], f"{p['units']} {p['unit']}s", n, f"{n / int(p['units']):.3f}",
                     f"{1000 * n / lines:.1f}" if lines else '-'])
    out.append(table(['date', 'probe', 'size', 'found', 'per unit', 'per 1k lines'], prow, md))

    # Feature-interaction coverage: new defects mostly come from two features meeting for the first time.
    try:
        import interaction_map as im
        pairs, used = im.scan()
        covered, total = im.summary(pairs)
        hist = im.history()
        out.append(h('Interaction coverage (pairs of language areas some passing program combines)'))
        out.append(f"{covered} of {total} pairs ({100 * covered / total:.1f}%) across {len(im.KEYS)} areas, "
                   f"{len(im.corpus())} programs. Untested pairs are where new defects come from; "
                   "a round that closes many of them and finds few defects is the sign the drop is near.")
        if hist:
            pct = [int(r['covered']) * 100 / int(r['total']) for r in hist] + [100 * covered / total]
            out.append(('`' if md else '') + sparkline(pct) + ('`' if md else '')
                       + f"  {hist[0]['label']} → now, {pct[0]:.1f}% → {pct[-1]:.1f}%")
            rows_h, prev = [], None
            for r in hist:
                c = int(r['covered'])
                rows_h.append([r['date'], r['label'], f"{c} / {r['total']}", f"{100 * c / int(r['total']):.1f}%",
                               '' if prev is None else f"+{c - prev}"])
                prev = c
            if prev is not None and covered != prev:
                rows_h.append([datetime.date.today().isoformat(), 'now', f'{covered} / {total}',
                               f'{100 * covered / total:.1f}%', f'+{covered - prev}'])
            out.append(table(['date', 'after', 'pairs covered', '%', 'new'], rows_h, md))
        worst = sorted(im.uncovered_by_area(pairs).items(), key=lambda kv: -kv[1])[:10]
        out.append('Most uncovered pairs, by area: ' + ', '.join(f'{k} {n}' for k, n in worst) + '.')
        # All-area triples: the campaign toward every triple accounted for (covered, or a held compile error).
        ac, at = im.all_triple_summary()
        ahist = im.all_triple_history()
        out.append(h('All-area triple coverage (every three areas meeting in one declaration)'))
        out.append(f"{ac} of {at} triples accounted for ({100 * ac / at:.1f}%) across {len(im.KEYS)} areas. A triple "
                   "counts when some passing program combines it; one that can't occur in valid code is removed from "
                   "the total and held by a negative test.")
        if ahist:
            pct = [int(r['covered']) * 100 / int(r['total']) for r in ahist]
            out.append(('`' if md else '') + sparkline(pct) + ('`' if md else '')
                       + f"  {ahist[0]['label']} → {ahist[-1]['label']}, {pct[0]:.1f}% → {pct[-1]:.1f}%")
            rows_a, prev = [], None
            for r in ahist:
                c = int(r['covered'])
                rows_a.append([r['date'], r['label'], f"{c} / {r['total']}", f"{100 * c / int(r['total']):.1f}%",
                               '' if prev is None else f"+{c - prev}"])
                prev = c
            out.append(table(['date', 'after', 'triples accounted for', '%', 'new'], rows_a, md))
        # Triples: with nearly every pair covered, the rounds aim at three areas meeting.
        tc, tt = im.triple_summary()
        thist = im.triple_history()
        out.append(h('Triple coverage (three focus areas meeting in one declaration)'))
        out.append(f"{tc} of {tt} triples ({100 * tc / tt:.1f}%) among {len(im.FOCUS)} focus areas: "
                   + ', '.join(im.FOCUS) + '.')
        if thist:
            pct = [int(r['covered']) * 100 / int(r['total']) for r in thist] + [100 * tc / tt]
            out.append(('`' if md else '') + sparkline(pct) + ('`' if md else '')
                       + f"  {thist[0]['label']} → now, {pct[0]:.1f}% → {pct[-1]:.1f}%")
            rows_t, prev = [], None
            for r in thist:
                c = int(r['covered'])
                rows_t.append([r['date'], r['label'], f"{c} / {r['total']}", f"{100 * c / int(r['total']):.1f}%",
                               '' if prev is None else f"+{c - prev}"])
                prev = c
            out.append(table(['date', 'after', 'triples covered', '%', 'new'], rows_t, md))
    except Exception as e:   # the report still runs if the map can't
        out.append(h('Interaction coverage'))
        out.append(f'(unavailable: {e})')

    today = datetime.date.today().isoformat()
    weeks = [week_of((datetime.date.today() - datetime.timedelta(weeks=k)).isoformat()) for k in range(11, -1, -1)]
    out.append(h('Last 12 weeks (week starting)'))
    out.append(table(['week'] + HEAD, period_rows(rows, weeks, week_of, churn_by(week_of)), md))

    churn = churn_by(lambda d: d[:7])
    months = sorted(set(r['found_date'][:7] for r in rows if r['found_date']) | {m for m in churn if m <= today[:7]})
    out.append(h('By month'))
    out.append(table(['month'] + HEAD, period_rows(rows, months, lambda d: d[:7], churn), md))

    if by:
        out.append(h(f'By {by}'))
        out.append(table([by, 'all', 'open'], [[k, v, sum(1 for r in rows if r[by] == k and not r['fixed_date'])]
                                               for k, v in Counter(r[by] for r in rows).most_common()], md))
    if md:
        out.append('\n*dev* = exposed by code being written; *probe* = doc-tests, feature matrix, field trials, suite; '
                   '*escape* = Jim or a WIP game hit it first. An *active week* has a compiler or BLR change, a defect '
                   'found, or a probe round; the headline and trend count only those, so time away changes nothing. '
                   'In the trend, *fixed* counts fixes since the previous active week and *open* the defects found '
                   'by the end of the week and not yet fixed then. '
                   '*New work* = lines added to the compiler, BLR, field trials, examples and WIP games, less '
                   'regression tests and commits that name a ledger ID (fixes). '
                   'Lines changed = compiler + BLR, from git plus the '
                   'uncommitted working tree; commits are batched, so short periods are lumpy. Lines are '
                   f'counted from {START}, when Beguile\'s library began; the repository\'s earlier prototype '
                   'history is not Beguile\'s. Generated by '
                   '`beguiler/tests/tools/readiness.py` from `tests/defects/ledger.csv`.')
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--by', choices=['area', 'severity', 'found_by'])
    ap.add_argument('--open', action='store_true', help='list open defects')
    ap.add_argument('--markdown', action='store_true')
    a = ap.parse_args()
    rows = list(csv.DictReader(open(os.path.join(DEFECTS, 'ledger.csv'), encoding='utf-8')))
    probes = list(csv.DictReader(open(os.path.join(DEFECTS, 'probes.csv'), encoding='utf-8')))
    if a.open:
        for r in rows:
            if not r['fixed_date']:
                print(f"{r['id']}  {r['found_date'] or '?':10}  {r['area']:12} {r['summary']}")
        return
    print(report(rows, probes, a.by, a.markdown))


if __name__ == '__main__':
    main()
