#!/bin/bash
# run_tests.sh — compile all test .bgl files and compare against baselines
# Usage:
#   ./run_tests.sh              — run tests, compare against baselines
#   ./run_tests.sh --capture    — compile and save baselines (first run or after intentional changes)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BEGUILER="$SCRIPT_DIR/../beguiler"
BASELINE_DIR="$SCRIPT_DIR/baselines"
OUTPUT_DIR="$SCRIPT_DIR/output"
TEST_FILES="$SCRIPT_DIR"/test_*.bgl

mkdir -p "$OUTPUT_DIR"

CAPTURE=false
if [ "$1" = "--capture" ]; then
    CAPTURE=true
    mkdir -p "$BASELINE_DIR"
fi

PASS=0
FAIL=0
ERRORS=0

for bgl in $TEST_FILES; do
    name=$(basename "$bgl" .bgl)
    inf="$SCRIPT_DIR/${name}.bgl.transpiled.inf"

    # Compile
    cd "$SCRIPT_DIR"
    "$BEGUILER" "$bgl" 2>/dev/null

    if [ ! -f "$inf" ]; then
        echo "  ERROR: $name — compilation failed (no .inf produced)"
        ERRORS=$((ERRORS + 1))
        continue
    fi

    if [ "$CAPTURE" = true ]; then
        cp "$inf" "$BASELINE_DIR/${name}.inf.baseline"
        echo "  CAPTURED: $name"
        rm -f "$inf" "${bgl}.bgldbg" "${bgl}.transpiled.inf.dbg" "${bgl}.transpiled.inf.map" "${bgl}.transpiled.inf.bgldbg"
    else
        baseline="$BASELINE_DIR/${name}.inf.baseline"
        if [ ! -f "$baseline" ]; then
            echo "  SKIP: $name — no baseline (run with --capture first)"
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
        rm -f "$inf" "${bgl}.bgldbg" "${bgl}.transpiled.inf.dbg" "${bgl}.transpiled.inf.map" "${bgl}.transpiled.inf.bgldbg"
    fi
done

echo ""
if [ "$CAPTURE" = true ]; then
    echo "Baselines captured."
else
    echo "Results: $PASS passed, $FAIL failed, $ERRORS errors"
    if [ $FAIL -gt 0 ] || [ $ERRORS -gt 0 ]; then
        exit 1
    fi
fi
