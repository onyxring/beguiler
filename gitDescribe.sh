#!/bin/sh
# Writes the release tag this source is built from into a header, for version.cpp.
# usage: gitDescribe.sh <header>
out="$1"
described=$(git describe --tags --dirty 2>/dev/null)
line="#define BEGUILER_GIT_DESCRIBE \"$described\""
mkdir -p "$(dirname "$out")"
if [ "$(cat "$out" 2>/dev/null)" != "$line" ]; then
    echo "$line" > "$out"
fi
