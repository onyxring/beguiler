#!/bin/bash
# run_tests.sh — Beguile compiler regression suite.
#
# Two test categories live side-by-side, each available for both source extensions:
#   • Positive baselines: test_*.bgl, test_*.inf  — must compile; .transpiled.inf must match
#                                                    captured baseline at <baselineDir>/<basename>.baseline.
#                                                    May also declare `// EXPECT_WARNING: <substring>`
#                                                    lines, each of which must appear on stderr — a
#                                                    warning never reaches the .inf, so a baseline
#                                                    alone cannot tell if one stops firing.
#   • Negative tests:    _test_*.bgl, _test_*.inf — must FAIL compilation; stderr must contain
#                                                    a marker string declared inline at the top
#                                                    of the file as `// EXPECT_ERROR: <substring>`
#                                                    (`! EXPECT_ERROR:` for .inf since `//` isn't
#                                                    an I6 comment). Multiple lines may be supplied;
#                                                    every substring must appear in stderr.
#   • Execution tests:   run/run_*.bgl                — must compile, pass Inform 6, AND produce the
#                                                    output declared inline as `// EXPECT_OUTPUT:
#                                                    <substring>`. Baselines compare emitted TEXT, so
#                                                    they cannot see invalid I6 or wrong behaviour;
#                                                    this tier is the only one that runs the program.
#                                                    Skipped with a notice when the I6 compiler or a
#                                                    Z-machine interpreter is unavailable.
#   • Example smoke:     ../examples/*.bgl            — must compile. Examples are shipped docs and
#                                                    used to rot unnoticed.
#
# The execution tier needs two external tools, discovered in this order:
#   Inform 6:     $INFORM6, then `inform6` on PATH, then ../../inform6/inform6
#   Interpreter:  $ZVM,     then `zvm` on PATH,     then ../../beguilex/node_modules/.bin/zvm
#
# Usage:
#   ./run_tests.sh              — run all tests
#   ./run_tests.sh --capture    — re-capture positive baselines (after intentional emission changes).
#                                 Negative tests are unaffected by --capture.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BEGUILER="$SCRIPT_DIR/../beguiler"
BASELINE_DIR="$SCRIPT_DIR/baselines"
OUTPUT_DIR="$SCRIPT_DIR/output"

mkdir -p "$OUTPUT_DIR"

CAPTURE=false
if [ "$1" = "--capture" ]; then
    CAPTURE=true
    mkdir -p "$BASELINE_DIR"
fi

PASS=0
FAIL=0
ERRORS=0

# ─── Positive baselines: test_*.{bgl,inf} ─────────────────────────────────────
shopt -s nullglob
for src in "$SCRIPT_DIR"/test_*.bgl "$SCRIPT_DIR"/test_*.inf; do
    # Skip compiler-emitted artifacts from prior runs that the .inf glob would otherwise match.
    case "$src" in *.transpiled.inf) continue;; esac

    name=$(basename "$src")          # full filename; baselines key off this
    # The compiler now writes the transpiled .inf and all debug artifacts into the output
    # directory (outputPath defaults to "output" relative to the source), not beside the source.
    inf="$OUTPUT_DIR/${name}.transpiled.inf"
    cleanup() { rm -f "$inf" "$OUTPUT_DIR/${name}.bgldbg" "$OUTPUT_DIR/${name}.transpiled.inf.dbg" "$OUTPUT_DIR/${name}.transpiled.inf.map" "$OUTPUT_DIR/${name}.transpiled.inf.bgldbg"; }

    # Compile. Force the output directory with -o (overrides each fixture's own
    # #beguilerSettings outputPath) so the transpiled .inf lands at a path we can find,
    # now that the compiler writes intermediates into the output directory.
    cd "$SCRIPT_DIR"
    compile_stderr=$("$BEGUILER" -o "$OUTPUT_DIR" "$src" 2>&1 >/dev/null)

    if [ ! -f "$inf" ]; then
        echo "  ERROR: $name — compilation failed (no .inf produced)"
        ERRORS=$((ERRORS + 1))
        continue
    fi

    if [ "$CAPTURE" = true ]; then
        cp "$inf" "$BASELINE_DIR/${name}.baseline"
        echo "  CAPTURED: $name"
        cleanup
    else
        # Diagnostics a fixture expects on stderr. Warnings never reach the .inf, so a
        # baseline cannot pin one — without this a warning could stop firing and every
        # test would still pass.
        want_warn=$(grep -E '^[[:space:]]*(//|!)[[:space:]]*EXPECT_WARNING:' "$src" \
            | sed -E -e 's|^[[:space:]]*//[[:space:]]*EXPECT_WARNING:[[:space:]]*||' \
                     -e 's|^[[:space:]]*![[:space:]]*EXPECT_WARNING:[[:space:]]*||')
        if [ -n "$want_warn" ]; then
            while IFS= read -r needle; do
                [ -z "$needle" ] && continue
                if ! echo "$compile_stderr" | grep -qF "$needle"; then
                    echo "  FAIL: $name — expected warning not emitted: '$needle'"
                    FAIL=$((FAIL + 1))
                    cleanup
                    continue 2
                fi
            done <<< "$want_warn"
        fi

        baseline="$BASELINE_DIR/${name}.baseline"
        if [ ! -f "$baseline" ]; then
            echo "  SKIP: $name — no baseline (run with --capture first)"
            cleanup
            continue
        fi

        if diff -q "$inf" "$baseline" > /dev/null 2>&1; then
            echo "  PASS: $name"
            PASS=$((PASS + 1))
        else
            echo "  FAIL: $name — output differs from baseline"
            diff "$inf" "$baseline" | head -20
            FAIL=$((FAIL + 1))
        fi
        cleanup
    fi
done
shopt -u nullglob

# ─── Negative tests: _test_*.{bgl,inf} ────────────────────────────────────────
# Each must FAIL compilation. Each must declare one or more EXPECT_ERROR markers
# at the top of the file. For .bgl use `// EXPECT_ERROR: …`; for .inf use
# `! EXPECT_ERROR: …` (I6 comment syntax — `//` is not valid in I6).
# Negative tests are not affected by --capture.
shopt -s nullglob
for src in "$SCRIPT_DIR"/_test_*.bgl "$SCRIPT_DIR"/_test_*.inf; do
    name=$(basename "$src")

    # Collect declared expected-error substrings. Accept either comment style.
    expected=$(grep -E '^[[:space:]]*(//|!)[[:space:]]*EXPECT_ERROR:' "$src" \
        | sed -E -e 's|^[[:space:]]*//[[:space:]]*EXPECT_ERROR:[[:space:]]*||' \
                 -e 's|^[[:space:]]*![[:space:]]*EXPECT_ERROR:[[:space:]]*||')
    if [ -z "$expected" ]; then
        echo "  ERROR: $name — negative test missing 'EXPECT_ERROR:' marker"
        ERRORS=$((ERRORS + 1))
        continue
    fi

    # Compile, capturing stderr. Expect non-zero exit. Force the output dir (see positive loop).
    cd "$SCRIPT_DIR"
    stderr=$("$BEGUILER" -o "$OUTPUT_DIR" "$src" 2>&1 >/dev/null)
    exit_code=$?
    inf="$OUTPUT_DIR/${name}.transpiled.inf"
    rm -f "$inf" "$OUTPUT_DIR/${name}.bgldbg" "$OUTPUT_DIR/${name}.transpiled.inf.dbg" "$OUTPUT_DIR/${name}.transpiled.inf.map" "$OUTPUT_DIR/${name}.transpiled.inf.bgldbg"

    if [ $exit_code -eq 0 ]; then
        echo "  FAIL: $name — expected compilation to fail, but it succeeded"
        FAIL=$((FAIL + 1))
        continue
    fi

    # Check every expected substring is present.
    all_matched=true
    while IFS= read -r needle; do
        [ -z "$needle" ] && continue
        if ! echo "$stderr" | grep -qF "$needle"; then
            echo "  FAIL: $name — stderr did not contain expected '$needle'"
            echo "    stderr: $(echo "$stderr" | head -3)"
            all_matched=false
            break
        fi
    done <<< "$expected"

    if [ "$all_matched" = true ]; then
        echo "  PASS: $name (negative)"
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
    fi
done
shopt -u nullglob

# ─── Example smoke: ../examples/*.bgl ─────────────────────────────────────────
# Examples are shipped documentation. They are not baselined (their emission is
# free to drift), but they must always compile.
if [ "$CAPTURE" != true ]; then
    shopt -s nullglob
    for src in "$SCRIPT_DIR"/../examples/*.bgl; do
        name=$(basename "$src")
        cd "$SCRIPT_DIR"
        if err=$("$BEGUILER" -o "$OUTPUT_DIR" "$src" 2>&1 >/dev/null); then
            echo "  PASS: $name (example)"
            PASS=$((PASS + 1))
        else
            echo "  FAIL: $name — example no longer compiles"
            echo "    $(echo "$err" | grep -iE 'ERROR' | head -1)"
            FAIL=$((FAIL + 1))
        fi
        rm -f "$OUTPUT_DIR/${name}.transpiled.inf" "$OUTPUT_DIR/${name}.bgldbg" \
              "$OUTPUT_DIR/${name}.transpiled.inf.dbg" "$OUTPUT_DIR/${name}.transpiled.inf.map" \
              "$OUTPUT_DIR/${name}.transpiled.inf.bgldbg"
    done
    shopt -u nullglob
fi

# ─── Execution tests: run/run_*.bgl ───────────────────────────────────────────
# The only tier that RUNS the program. Baselines compare emitted text, so they
# pass happily on I6 that will not assemble and on logic that is simply wrong.
if [ "$CAPTURE" != true ]; then
    RUN_DIR="$SCRIPT_DIR/run"
    INFORM6="${INFORM6:-$(command -v inform6 || echo "$SCRIPT_DIR/../../inform6/inform6")}"
    ZVM="${ZVM:-$(command -v zvm || echo "$SCRIPT_DIR/../../beguilex/node_modules/.bin/zvm")}"

    shopt -s nullglob
    run_tests=("$RUN_DIR"/run_*.bgl)
    shopt -u nullglob

    if [ ${#run_tests[@]} -gt 0 ]; then
        echo ""
        if [ ! -x "$INFORM6" ]; then
            echo "Execution tests SKIPPED — no Inform 6 compiler (set \$INFORM6)."
        elif [ ! -x "$ZVM" ] && [ ! -f "$ZVM" ]; then
            echo "Execution tests SKIPPED — no Z-machine interpreter (set \$ZVM)."
        else
            echo "Running execution tests..."
            RUN_OUT="$OUTPUT_DIR/run"
            mkdir -p "$RUN_OUT"
            for src in "${run_tests[@]}"; do
                name=$(basename "$src")
                expected=$(grep -E '^[[:space:]]*//[[:space:]]*EXPECT_OUTPUT:' "$src" \
                    | sed -E 's|^[[:space:]]*//[[:space:]]*EXPECT_OUTPUT:[[:space:]]*||')
                if [ -z "$expected" ]; then
                    echo "  ERROR: $name — execution test missing 'EXPECT_OUTPUT:' marker"
                    ERRORS=$((ERRORS + 1)); continue
                fi

                rm -rf "${RUN_OUT:?}"/*
                cd "$SCRIPT_DIR"
                if ! err=$("$BEGUILER" -o "$RUN_OUT" "$src" 2>&1 >/dev/null); then
                    echo "  ERROR: $name — transpile failed"
                    echo "    $(echo "$err" | grep -iE 'ERROR' | head -1)"
                    ERRORS=$((ERRORS + 1)); continue
                fi
                inf="$RUN_OUT/${name}.transpiled.inf"
                story="$RUN_OUT/${name}.z5"
                # Inform 6 reports errors on stdout and still exits 0 in some builds,
                # so the story file's existence is what decides success.
                i6out=$("$INFORM6" -v5 "$inf" "$story" 2>&1)
                if [ ! -f "$story" ]; then
                    echo "  FAIL: $name — Inform 6 rejected the emitted code"
                    echo "    $(echo "$i6out" | grep -iE 'error' | head -2)"
                    FAIL=$((FAIL + 1)); continue
                fi

                actual=$(echo "" | "$ZVM" "$story" 2>&1)
                all_matched=true
                while IFS= read -r needle; do
                    [ -z "$needle" ] && continue
                    if ! echo "$actual" | grep -qF "$needle"; then
                        echo "  FAIL: $name — output did not contain '$needle'"
                        echo "    got: $(echo "$actual" | grep -vE '^$' | head -3 | tr '\n' '|')"
                        all_matched=false; break
                    fi
                done <<< "$expected"

                # A Z-machine trap prints a diagnostic and keeps going, so matching the
                # expected text is not on its own proof the run was clean.
                if [ "$all_matched" = true ] && echo "$actual" | grep -q "Programming error"; then
                    echo "  FAIL: $name — run produced a Z-machine programming error"
                    echo "    $(echo "$actual" | grep 'Programming error' | head -1)"
                    all_matched=false
                fi

                if [ "$all_matched" = true ]; then
                    echo "  PASS: $name (execution)"
                    PASS=$((PASS + 1))
                else
                    FAIL=$((FAIL + 1))
                fi
            done
            rm -rf "$RUN_OUT"
        fi
    fi
fi

echo ""
if [ "$CAPTURE" = true ]; then
    echo "Baselines captured."
    echo "(Negative tests run on every invocation; --capture only affects positive baselines.)"
else
    # Debug-bundle soundness — static .bgldbg validator over the tests/debug/ corpus.
    DBG_FAIL=0
    if [ -f "$SCRIPT_DIR/validate_bgldbg.py" ] && command -v python3 >/dev/null 2>&1; then
        echo ""
        echo "Validating .bgldbg debug bundles..."
        if ! python3 "$SCRIPT_DIR/validate_bgldbg.py"; then DBG_FAIL=1; fi
    fi
    echo ""
    echo "Results: $PASS passed, $FAIL failed, $ERRORS errors"
    if [ $FAIL -gt 0 ] || [ $ERRORS -gt 0 ] || [ $DBG_FAIL -gt 0 ]; then
        exit 1
    fi
fi
