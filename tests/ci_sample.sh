#!/bin/bash
# ci_sample.sh — transpiles the programs in ci-sample.txt and compares each with its baseline.
# The full suite (run_tests.sh) runs on the developer's machine; this checks that a build on
# another platform produces the same output. Needs no Inform 6, library or interpreter.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BEGUILER="${BEGUILER:-$SCRIPT_DIR/../beguiler}"
[ -f "$BEGUILER.exe" ] && BEGUILER="$BEGUILER.exe"
OUT="$SCRIPT_DIR/output/ci"
export BEGUILER_VERSION="v0.0.5"     # the version the baselines were captured with
mkdir -p "$OUT"
cd "$SCRIPT_DIR"
pass=0; fail=0
while read -r name; do
    case "$name" in ''|'#'*) continue;; esac
    inf="$OUT/$name.transpiled.inf"
    rm -f "$inf"
    "$BEGUILER" -inform=none -o "$OUT" "$name" > "$OUT/$name.log" 2>&1
    if [ ! -f "$inf" ]; then
        echo "  ERROR: $name — nothing transpiled"; tail -5 "$OUT/$name.log"; fail=$((fail + 1)); continue
    fi
    # Windows writes \r\n line ends, which Inform 6 reads like \n.
    if diff -q --strip-trailing-cr "$inf" "baselines/$name.baseline" > /dev/null; then
        pass=$((pass + 1))
    else
        echo "  FAIL: $name — output differs from the baseline"
        diff --strip-trailing-cr "$inf" "baselines/$name.baseline" | head -20
        fail=$((fail + 1))
    fi
done < ci-sample.txt
echo "CI sample: $pass match, $fail differ"
[ "$fail" -eq 0 ]
