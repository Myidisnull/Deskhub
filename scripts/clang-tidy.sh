#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR=${1:-out/build/x64-debug}
DB="$BUILD_DIR/compile_commands.json"
CLANG_TIDY=${CLANG_TIDY:-clang-tidy}

command -v "$CLANG_TIDY" >/dev/null 2>&1 || {
    echo "clang-tidy.sh: '$CLANG_TIDY' not found." >&2
    exit 1
}

if [ ! -f "$DB" ]; then
    echo "clang-tidy.sh: $DB is missing - run 'cmake --preset x64-debug' first." >&2
    exit 1
fi

FILES=$(python3 - "$DB" <<'EOF'
import json, sys

with open(sys.argv[1]) as db:
    entries = json.load(db)

wanted = [e["file"] for e in entries if "/core/src/" in e["file"] or "/platform/src/" in e["file"]]
print("\n".join(sorted(set(wanted))))
EOF
)

if [ -z "$FILES" ]; then
    echo "clang-tidy.sh: no core/platform sources in $DB." >&2
    exit 1
fi

echo "[clang-tidy] $(echo "$FILES" | grep -c .) files ($(command -v "$CLANG_TIDY"))"
echo "$FILES" | xargs "$CLANG_TIDY" -p "$BUILD_DIR" --quiet
