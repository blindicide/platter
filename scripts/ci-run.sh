#!/usr/bin/env bash
# Runs a CI command and, if it fails, re-emits the interesting part of its
# output as GitHub Actions error annotations. Annotations are readable through
# the public checks API, so failures can be diagnosed without raw log access.
#
# usage: scripts/ci-run.sh "<title>" <command> [args...]
set -uo pipefail

title="$1"
shift
log="$(mktemp)"

"$@" 2>&1 | tee "$log"
rc=${PIPESTATUS[0]}

if [ "$rc" -ne 0 ]; then
    # Error-looking lines first (compilers, CMake, Catch2), then the tail.
    errors="$(grep -nE 'error|Error|ERROR|FAILED|failed|CMake Error|fatal' "$log" | head -n 40)"
    tail_lines="$(tail -n 40 "$log")"
    encode() { sed -e 's/%/%25/g' -e 's/\r//g' | awk '{printf "%s%%0A", $0}'; }
    if [ -n "$errors" ]; then
        echo "::error title=${title} (matching lines)::$(printf '%s\n' "$errors" | encode)"
    fi
    echo "::error title=${title} (last 40 lines)::$(printf '%s\n' "$tail_lines" | encode)"
fi
rm -f "$log"
exit "$rc"
