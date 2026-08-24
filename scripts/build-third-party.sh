#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

LIBRARY=${1:-}
case "$LIBRARY" in
    quiche | opus) shift ;;
    *)
        echo "build-third-party.sh: name the library first - quiche or opus." >&2
        echo "                      usage: build-third-party.sh <library> [target...]" >&2
        exit 1
        ;;
esac

LOG="$LIBRARY-build.log"

if "scripts/build-$LIBRARY.sh" "$@" 2>&1 | tee "$LOG"; then
    exit 0
fi

grep -iE "error|LNK[0-9]|panicked" "$LOG" | tail -30 | sed 's/^/::error::/' || true
tail -15 "$LOG" | sed 's/^/::error::/' || true
exit 1
