# BUG: `.bgldbg [map]` i6Line is off-by-one for user-routine statements

**Status:** ✅ FIXED (Jack, 2026-08-16), verified line-exact. Compiler-side (emission), pre-existing,
separate from the superposed-anchor bug. Found by Jack while fixing that bug
([BUG_superposed_sourcemap_anchor.md](BUG_superposed_sourcemap_anchor.md)).

## Root cause + fix (2026-08-16)
Not the statement accounting — a **marker-substitution desync**. `resolvedOutput()` replaces the
whole-line `#storedEmitFirst`/`#storedEmitLast` placeholders (`kStoredFirstMarker`, one line in the emit
buffer the sourceMap is built against) with the concatenated fired blocks. When nothing fires — the
common case, e.g. a program that uses no literal-list `for-in`, so the always-registered `scratchSupport`
block doesn't fire — the marker (1 line) is replaced by **empty** (0 lines), shifting every later `.inf`
line up by one while the map still holds pre-substitution line numbers. Hence the uniform +1, present in
any program regardless of `superposed`.
Fix: in the `substitute` lambda, compute `delta = contentLines - markerLines` and re-base every sourceMap
entry below the marker by `delta` before/at the replace. (Superposed blocks are appended *after* the
substitutions, so their offsets already see the corrected buffer.) Verified: the header and every
statement now map to their exact `.inf` line. Regression guard added to `tests/validate_bgldbg.py`: each
user function's decl must map to the exact `.inf` line of its `[name` routine header (a whole-map shift
trips it even though every entry still points at real code). Proven to fail on a +1 map, pass after;
corpus 5/5, suite 174/0/0.

## One-line summary
Every statement in a user routine is stamped with an `i6Line` that is **one greater** than the `.inf`
line the statement is actually emitted on. The `bglLine` column is correct; the pairing is off by one
on the `.inf` side.

## Reproduction
```
cat > /tmp/ml.bgl <<'EOF'
#beguilerSettings { target = z5; informName = "none"; outputPath = "out"; }
void main(){
    int a = 1;      // source line 3
    int b = 2;      // source line 4
    int c = a + b;  // source line 5
    print(c);       // source line 6
}
EOF
./beguiler /tmp/ml.bgl --debug
```

`.inf` layout vs `[map]`:

| `.inf` line | content    | source line | `[map]` says |
|-------------|------------|-------------|--------------|
| 219         | `[main a b c;` (header) | — | — |
| 220         | `a = 1;`    | **3**       | bgl **2** (should be 3) |
| 221         | `b = 2;`    | **4**       | bgl **3** (should be 4) |
| 222         | `c=a + b;`  | **5**       | bgl **4** (should be 5) |
| 223         | `print c;`  | **6**       | bgl **5** (should be 6) |

Equivalently: the entry whose `bglLine` is 3 (statement `a = 1;`, emitted on `.inf` 220) is recorded
with `i6Line` 221 — the `.inf` line of the *next* statement. Confirmed present in the committed binary
(pre-fix), so it is not introduced by the superposed-anchor change.

## Effect in the debugger
- **Stepping (forward):** landing on a statement's real `.inf` line shows the *previous* source line.
- **Breakpoints (reverse):** a breakpoint on a source line binds to the *following* statement's address.

Off-by-one, but (unlike the anchor bug) the halves still join — every entry points at real code, just
shifted one line — so it is easy to miss without a line-exact assertion.

## Where to look
`emitStatement` (`i6Emitter.cpp`) pushes `{currentLine(), stmt->src.file, stmt->src.line}` *before*
writing the statement text, so `currentLine()` should be the line about to be written. The observed +1
means `currentLine()` is one high at that moment — i.e. one extra newline has been counted between the
routine header and the first statement (and the shift then rides along for the rest). Candidates:
the header emission (`[name … ;\n`) vs. where the first `emitStatement` samples `currentLine()`, the
frame-alloc / spill prologue, or a stray `\n`. Needs a trace of `currentLine()` vs. the emitted line at
each `emitStatement` for a no-spill routine.

## Validator coverage
The `.bgldbg` validator (`tests/validate_bgldbg.py`) deliberately does **not** fail on this yet — it
tolerates single stray structural entries so the superposed-anchor *cluster* check stays meaningful. A
line-exact check ("each `[map]` entry's `.inf` line contains code corresponding to its `bglLine`") is the
natural regression guard to add once this is fixed. Jill's DAP harness would catch it as a one-line
breakpoint/step skew.
