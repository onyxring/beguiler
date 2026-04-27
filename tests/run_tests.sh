#!/bin/bash
# run_tests.sh — Beguile compiler regression suite.
#
# Two test categories live side-by-side, each available for both source extensions:
#   • Positive baselines: test_*.bgl, test_*.inf  — must compile; .transpiled.inf must match
#                                                    captured baseline at <baselineDir>/<basename>.baseline.
#   • Negative tests:    _test_*.bgl, _test_*.inf — must FAIL compilation; stderr must contain
#                                                    a marker string declared inline at the top
#                                                    of the file as `// EXPECT_ERROR: <substring>`
#                                                    (`! EXPECT_ERROR:` for .inf since `//` isn't
#                                                    an I6 comment). Multiple lines may be supplied;
#                                                    every substring must appear in stderr.
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
    inf="${src}.transpiled.inf"
    cleanup() { rm -f "$inf" "${src}.bgldbg" "${src}.transpiled.inf.dbg" "${src}.transpiled.inf.map" "${src}.transpiled.inf.bgldbg"; }

    # Compile
    cd "$SCRIPT_DIR"
    "$BEGUILER" "$src" 2>/dev/null

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

    # Compile, capturing stderr. Expect non-zero exit.
    cd "$SCRIPT_DIR"
    stderr=$("$BEGUILER" "$src" 2>&1 >/dev/null)
    exit_code=$?
    inf="${src}.transpiled.inf"
    rm -f "$inf" "${src}.bgldbg" "${src}.transpiled.inf.dbg" "${src}.transpiled.inf.map" "${src}.transpiled.inf.bgldbg"

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

echo ""
if [ "$CAPTURE" = true ]; then
    echo "Baselines captured."
    echo "(Negative tests run on every invocation; --capture only affects positive baselines.)"
else
    echo "Results: $PASS passed, $FAIL failed, $ERRORS errors"
    if [ $FAIL -gt 0 ] || [ $ERRORS -gt 0 ]; then
        exit 1
    fi
fi
