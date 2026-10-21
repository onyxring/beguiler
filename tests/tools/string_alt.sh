#!/bin/bash
# Runs the execution tests and field trials with <stringObj> standing in for <string>: a copy of beguiLib
# whose string.bgl is stringObj.bgl, used through a beguiler wrapper. Baselines are skipped, since the two
# extensions emit different code.
#
# Usage: tools/string_alt.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="$(mktemp -d -t string-alt)"
trap 'rm -rf "$WORK"' EXIT

cp -R "$ROOT/beguiLib" "$WORK/beguiLib"
cp "$ROOT/beguiLib/stringObj.bgl" "$WORK/beguiLib/string.bgl"
cat > "$WORK/beguiler" <<WRAP
#!/bin/bash
exec "$ROOT/beguiler" -lib="$WORK/beguiLib" "\$@"
WRAP
chmod +x "$WORK/beguiler"
export BEGUILER="$WORK/beguiler"

# run_tests.sh also runs the spec doc tests and the field trials. Baseline differences are expected.
LOG="${STRING_ALT_LOG:-$WORK/log.txt}"
"$ROOT/tests/run_tests.sh" > "$LOG" 2>&1 || true
grep -E "FAIL|ERROR|Results|Field trials|Spec doc tests" "$LOG" | grep -v "differs from baseline" || true
