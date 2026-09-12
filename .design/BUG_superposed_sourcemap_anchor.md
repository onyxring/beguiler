# BUG: `.bgldbg [map]` anchors superposed/spliced routine bodies onto the wrong `.inf` line

**Status:** ✅ FIXED (Jack, 2026-08-16), verified on Jill's repro. Compiler-side (emission). Filed by
Jill (adapter side) for Jack (emission side). Originates from Jack's own probe note ("superposed
routine map lines jump 220 → 3 → 5 because bodies are captured and spliced with local line numbers").
Jim's call: address it, don't dismiss on "you rarely step into core."

## Resolution (2026-08-16)
Routed the captured `superposed` body's sourceMap pushes into a per-block buffer (`sourceMapTarget` →
`superposedBlockMaps[name]`) with **capture-relative** i6Lines, skipped the coarse main-pass anchor for
superposed nodes (`generateI6`, gated on `isSuperposed`), and re-based each block's entries by its
splice offset when `resolvedOutput()` appends it (`i6Line += count('\n' in buf-before-append)`). Works
because `writeFile`/`resolvedOutput` (which now also corrects the map) runs before `writeDebugBundle`.
Verified on the repro: the 13 `_math.bgl` routine lines that all mapped to the bare-`;` anchor now map
to their real `.inf` code (`[_bglMax a b;` / `if (a > b) {` / `return a;` …). Suite 174/0/0.

**Regression guard:** added `tests/validate_bgldbg.py` (+ `tests/debug/` corpus incl. `dbg_superposed`)
wired into `run_tests.sh` — static checks on the bundle (structure, referenced files exist, bglLine and
i6Line in range, user-function coverage, user code mapped). It does NOT yet implement the exact
sequence-point subset check below (that needs the I6 `.dbg` from a *library-backed* full compile — a
Beguile program has no `Main` without a binding, so the fast informName=none corpus can't produce one).
The *strict* structural "every map line must be executable" proxy was rejected: it false-positives on
legitimate single map targets (a `}` closing an `if`, a routine's `];`, blank lines inside routines all
legitimately receive one entry), so it is not a valid standalone invariant without the `.dbg`. A
**cluster-scoped** version IS wired in, though, and sidesteps that: it flags only when **≥3** map
entries land on one non-executable line — the anchor bug's signature (this bug put 11–55 statements on a
single splice line) — while tolerating the legitimate single entries. Verified: fails on the pre-fix
binary (39-entry clusters), passes after; corpus 5/5. (The stray single structural entries that remain
are the separate off-by-one, [BUG_sourcemap_offbyone.md](BUG_sourcemap_offbyone.md).)

---

## One-line summary
For `superposed` core-library routines (and orLibrary-bound routines), the `[map]` section of
`.bgldbg` records the routine's statements against a **splice-anchor `.inf` line** — a non-executable
`object`-decl terminator or `#include` line — instead of the real `.inf` line where the body is
emitted. Because that anchor line carries **no `<sequence-point>`** in the I6 `.dbg`, and the real code
lines carry **no `[map]` entry**, the two halves of the debug map **never join** for these routines.

Net effect in the debugger:
- **Breakpoints** on any superposed/core `.bgl` line → resolve to the anchor `.inf` line → 0 VM
  addresses → **silently never bind.**
- **Stepping** into one → the address resolves (via `.dbg`) to the real `.inf` line → that line has
  **no `[map]` entry** → address shown as unmapped / I6-only → **no `.bgl` line displayed** (not even
  a wrong one — the mapping is simply absent).

The `bglFile`/`bglLine` columns are **correct**; only the `i6Line` (first) column is wrong.

---

## Reproduction (fresh, coherent build — do not use stale artifacts; the `.inf`/`.dbg`/`.bgldbg` must
## come from a single `--debug` run or the line numbers won't correspond)

```
# minimal Glulx program whose initialise() calls a superposed core routine
cat > /tmp/spmap/t.bgl <<'EOF'
#beguilerSettings {
    target=Glulx;
    title="SPMap";
    includePaths ="/Users/jim/projects/IF-Projects/inform6/lib";
}
#includeI6 "parser"
#includeI6 "verblib"
void initialise(){
    int a = bgl.util.math.min(3, 7);
    int b = bgl.util.math.max(3, 7);
    print(a); print(b);
}
#includeI6 "grammar"
EOF
./beguiler --debug /tmp/spmap/t.bgl -lib=beguiLib -o /tmp/spmap/out
```

### Evidence (from that build's own artifacts)

`.inf` is 240 lines. `[map]` inform-line-column histogram (top hotspots):

| `.inf` line | # `[map]` entries | what that `.inf` line actually is | `<file-index>0` seq-points in `.dbg` |
|-------------|-------------------|-----------------------------------|--------------------------------------|
| **15**      | **55** (incl. all 13 `_math.bgl` routine lines) | `;` (terminator of `object _glulx`) | **0** |
| **17**      | 32                | `  with glulx _glulx`             | **0** |
| **20**      | 11                | (blank line inside a routine)     | **0** |
| 21          | 0                 | `        if(lhs < rhs) return -1;` (real code) | 2 |
| 22          | 0                 | real routine code                 | 2 |
| 23          | 0                 | real routine code                 | 1 |

All 13 distinct `_math.bgl` routine-body lines (`26, 39, 62, 68, 81, 82, 83, 89, 94, 95, 96, 97, 106`)
map to `.inf` line **15**. Line 15 is a bare `;` with zero sequence points. The routines' real code
(inf 21-23) has sequence points but no `[map]` entry. → the halves are disjoint.

Quick re-derivation:
```
# [map] inform-line hotspots
awk '/^\[map\]/{m=1;next} /^\[/{m=0} m' out/t.bgl.bgldbg | cut -f1 | sort -n | uniq -c | sort -rn | head
# content of an anchor line
awk 'NR==15' out/t.bgl.transpiled.inf          # -> ";"
# sequence-point coverage of that line (expect 0)
grep -c '<line>15</line>' out/t.bgl.transpiled.inf.dbg   # note: also check <file-index>0 context
```

---

## Where it is in the code

- `writeSourceMap` (`i6Emitter.cpp:530`) just dumps the `sourceMap` vector verbatim — not the bug.
- `currentLine()` (`i6Emitter.cpp:526`) = `count('\n' in out.str()) + 1` — **correct by construction**.
- The **correct model** is the newline-walking splice path (`i6Emitter.cpp:1244-1250`): it emits body
  text char-by-char and pushes `{currentLine(), srcStart.file, srcStart.line + srcOffset}` *as each
  newline is written*, so `i6Line` tracks the real output position. Superposed bodies are evidently
  **not** going through this (or an equivalent live-position) path.
- `sourceMap.push_back(...)` sites to audit: `926` (bglInit), `1162`, `1248` (the good splice path),
  `1451` (function anchor), `1564` (per-statement).

**Hypothesis (yours, corroborated):** the superposed routine bodies are captured as text at parse time
and their `sourceMap` entries are pushed while the emitter's `out` cursor is still in the
namespace-object declaration region (~inf 14-20), i.e. `currentLine()` is sampled **decoupled from**
the point where the body text is actually appended to `out` (which happens later, ~inf 200+). The
`bglLine` (`srcStart.line + offset`) stays right because it's computed from the captured source; the
`i6Line` is wrong because it's sampled at the wrong emission moment. Different anchor lines (15/17/20)
correspond to different capture/splice batches.

**Fix direction:** route captured superposed/spliced bodies through the same live-position accounting
as `1244-1250` — sample `currentLine()` at the moment each body line is written to `out`, not when the
statement is queued/captured. (orLibrary-bound routines anchoring onto `#include` lines are the same
class of decoupling.)

---

## Ready invariant for the `.bgldbg` validator (the "targeted check" you asked for)

Cheap, no interpreter, catches exactly this class:

> **Every `[map]` `i6Line` must carry ≥1 `<file-index>0` `<sequence-point>` in the `.dbg`.**
> (i.e. `{i6Line ∈ [map]} ⊆ {executable .inf lines}`)

Today lines 15/17/20 violate it. A stronger form also asserts the reverse coverage for user routines
(every routine's body lines that have sequence points also have `[map]` entries), but the subset
invariant above is the minimal repro of this bug.

The DAP-harness counterpart (Jill, once the shared corpus lands): a breakpoint set on a superposed
routine's `.bgl` line must bind to ≥1 VM address (today: 0); stepping into it must yield a defined
`.bgl` location.

---

## Adapter-side note (separate, latent — NOT this bug)
`debugInfo.ts` holds `infToBgl` as `Map<infLine, BglLocation>` (**last-write-wins**), so even a
*legitimately* inlined `.inf` line hosting multiple `.bgl` statements would keep only one. Won't fix
this bug (the anchor lines have no code at all), but it's a real lossiness in the same path — Jill will
address it (`Map<infLine, BglLocation[]>` + address-level disambiguation) when building the harness.

See memory `project_superposed_sourcemap_anchor_bug` for the full investigation trail.
