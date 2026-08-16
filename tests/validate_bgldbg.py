#!/usr/bin/env python3
"""
validate_bgldbg.py — static soundness checker for the .bgldbg debug bundle.

The .bgldbg bundle (emitted by `beguiler --debug`) is the map the VS Code debugger
uses to connect Beguile source to the running program. If it is wrong, breakpoints
land on the wrong line, variables show garbage, or nothing stops. This checker reads
the bundle (+ the transpiled .inf and the referenced source files) and asserts the
invariants that must hold for a *correct* bundle, so a compiler change that dents the
map turns the suite red immediately — before it reaches a debugging session.

It intentionally does NOT check things that legitimately vary in correct output
(e.g. the map's i6-line column is not globally monotonic, because `superposed`
core routines are captured and spliced with local line bases).

Bundle format (section-delimited):
  [map]    <i6Line> \t <bglFile> \t <bglLine>
  [sym]    <qualifiedName> \t <displayName> \t <kind>     kind in {global,object,property,function}
  [types]  `enum X` / `  value n v` / `type X` / `  prop n i6n type`

Usage:  tests/validate_bgldbg.py            (run from the beguiler root; validates tests/debug/*.bgl)
Exit status: 0 = all sound, 1 = at least one check failed.
"""
import os, re, subprocess, sys

ROOT     = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # beguiler/
BEGUILER = os.path.join(ROOT, "beguiler")
CORPUS   = os.path.join(ROOT, "tests", "debug")
OUTDIR   = os.path.join(CORPUS, "out")
VALID_KINDS = {"global", "object", "property", "function"}
CTRL = {"if", "for", "while", "switch", "return", "else", "catch", "do"}

_linecache = {}
def line_count(path):
    if path not in _linecache:
        try:
            with open(path, "r", errors="replace") as f:
                _linecache[path] = sum(1 for _ in f)
        except OSError:
            _linecache[path] = None      # missing file
    return _linecache[path]

def parse_bundle(text):
    section, out = None, {"map": [], "sym": [], "types": []}
    for raw in text.split("\n"):
        if raw in ("[map]", "[sym]", "[types]"):
            section = raw[1:-1]; continue
        if section and raw != "":
            out[section].append(raw)
    return out

def user_functions(src_path):
    """Top-level (column-0) function definitions in the user source: (name, decl_line)."""
    fns = []
    with open(src_path, "r", errors="replace") as f:
        for i, line in enumerate(f, 1):
            m = re.match(r'^([A-Za-z_]\w[\w<>]*)\s+([A-Za-z_]\w*)\s*\(', line)
            if m and m.group(1) not in CTRL:
                fns.append((m.group(2).lower(), i))
    return fns

def validate(bgl):
    name = os.path.basename(bgl)
    fails = []
    def check(cond, msg):
        if not cond: fails.append(msg)

    # compile with --debug (informName=none in the program → no I6 handoff, bundle still written)
    r = subprocess.run([BEGUILER, os.path.join("tests", "debug", name), "--debug"],
                       cwd=ROOT, capture_output=True, text=True)
    dbg = os.path.join(OUTDIR, name + ".bgldbg")
    inf = os.path.join(OUTDIR, name + ".transpiled.inf")
    if not os.path.exists(dbg):
        return [f"no .bgldbg produced (compile failed?):\n{r.stdout}{r.stderr}"]
    if not os.path.exists(inf):
        return ["no transpiled .inf produced"]

    b = parse_bundle(open(dbg, errors="replace").read())
    inf_lines = line_count(inf)
    inf_text  = open(inf, errors="replace").read()
    inf_lst   = inf_text.split("\n")
    # Non-executable .inf lines that can never legitimately host a routine body (blank or a bare
    # brace/terminator). A *cluster* of [map] entries on one of these is the superposed anchor bug.
    STRUCTURAL = {"", ";", "}", "};", "];", "{"}

    # A. structure --------------------------------------------------------------
    check(len(b["map"]) > 0, "[map] section is empty")
    for ln in b["sym"]:
        parts = ln.split("\t")
        check(len(parts) == 3 and parts[2] in VALID_KINDS, f"[sym] malformed line: {ln!r}")
    # [types] carries enum/value, type/prop, routine/local, and global entries.
    for ln in b["types"]:
        check(re.match(r'^(enum |type |routine |global |  value |  local |  prop )', ln) is not None,
              f"[types] malformed line: {ln!r}")
        # `local` lines carry a storage location so the debugger can find spilled/XP values:
        #   `  local <name> <type> <storage> [synthetic]`  storage = slot | _bglFrm-->N | _bglXPn
        if ln.startswith("  local "):
            check(re.match(r'^  local \S+ \S+ (slot|_bglFrm-->\d+|_bglXP\d+)( synthetic)?$', ln) is not None,
                  f"[types] local line missing/invalid storage location: {ln!r}")

    # parse map, well-formed rows only
    rows = []
    for ln in b["map"]:
        p = ln.split("\t")
        if len(p) != 3 or not p[0].isdigit() or not p[2].isdigit():
            fails.append(f"[map] malformed line: {ln!r}"); continue
        rows.append((int(p[0]), p[1], int(p[2])))

    # B/C/D. referenced files exist; bglLine + i6Line in range -------------------
    for i6, bglfile, bglline in rows:
        lc = line_count(bglfile)
        check(lc is not None, f"[map] references missing file: {bglfile}")
        if lc is not None:
            check(1 <= bglline <= lc, f"[map] bglLine {bglline} out of range for {os.path.basename(bglfile)} (has {lc} lines)")
        check(1 <= i6 <= inf_lines, f"[map] i6Line {i6} out of range for transpiled .inf (has {inf_lines} lines)")

    # G. superposed/spliced anchor bug — the splice-region defect Jill filed. A `superposed` routine's
    #    body was stamped onto a single non-executable splice line (a blank or bare terminator that
    #    carries no sequence point), so ALL its statements clustered there and breakpoints never bound.
    #    A correct build never *clusters* on a structural line. (Single stray structural entries come
    #    from a separate, pre-existing off-by-one in statement line accounting — see
    #    .design/BUG_sourcemap_offbyone.md — and are tolerated here rather than masking this check.)
    from collections import Counter
    struct_hits = Counter(i6 for i6, _, _ in rows
                          if 1 <= i6 <= len(inf_lst) and inf_lst[i6 - 1].strip() in STRUCTURAL)
    for i6, n in sorted(struct_hits.items()):
        check(n < 3, f"[map] {n} entries anchor onto non-executable .inf line {i6} "
                     f"('{inf_lst[i6-1].strip()}') — superposed/spliced source-map anchor cluster")

    # E. every top-level user function is emitted as a routine AND its line is mapped, LINE-EXACTLY:
    #    its declaration must map to the exact .inf line of its `[name` routine header. A whole-map
    #    shift (e.g. the #storedEmitFirst marker→empty off-by-one) breaks this even though every entry
    #    still points at real code, so it needs a line-exact assertion to catch.
    own_pairs = {(r[0], r[2]) for r in rows if os.path.realpath(r[1]) == os.path.realpath(bgl)}
    src_lines = {b for _, b in own_pairs}
    inf_lst   = inf_text.split("\n")
    for fn, decl_line in user_functions(bgl):
        hdr = next((i for i, ln in enumerate(inf_lst, 1)
                    if re.match(r'^\[ ?' + re.escape(fn) + r'\b', ln)), None)
        check(hdr is not None,
              f"user function '{fn}' has no routine in the transpiled .inf (dropped from debug output)")
        check(decl_line in src_lines,
              f"user function '{fn}' (source line {decl_line}) is not covered by the map — cannot break at it")
        if hdr is not None:
            check((hdr, decl_line) in own_pairs,
                  f"user function '{fn}' decl (bgl line {decl_line}) does not map to its routine header "
                  f".inf line {hdr} — line-exact map skew (off-by-one in statement line accounting)")

    # F. the user's own source is represented in the map ------------------------
    check(any(os.path.realpath(r[1]) == os.path.realpath(bgl) for r in rows),
          "user source file does not appear in the map (user code is entirely unmapped)")

    return fails

def main():
    if not os.path.isdir(CORPUS):
        print("no tests/debug corpus"); return 0
    os.makedirs(OUTDIR, exist_ok=True)
    corpus = sorted(f for f in os.listdir(CORPUS) if f.endswith(".bgl"))
    total_fail = 0
    for name in corpus:
        fails = validate(os.path.join(CORPUS, name))
        if fails:
            total_fail += 1
            print(f"  FAIL: {name}")
            for m in fails: print("        - " + m.replace("\n", "\n          "))
        else:
            print(f"  PASS: {name}")
    print(f"\n.bgldbg validation: {len(corpus) - total_fail} passed, {total_fail} failed, {len(corpus)} total")
    return 1 if total_fail else 0

if __name__ == "__main__":
    sys.exit(main())
