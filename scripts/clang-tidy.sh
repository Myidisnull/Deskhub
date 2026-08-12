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

TIDY_ARGS=(-p "$BUILD_DIR" --quiet)
SYSROOT_NOTE=""

# CMake leaves -isysroot out of the compile database on macOS, and a clang-tidy that did
# not ship with Xcode has no default SDK, so every standard header and framework goes
# missing and the broken AST produces nonsense diagnostics. CI runs on Linux, which needs
# none of this.
if [ "$(uname -s)" = "Darwin" ]; then
    SDK=$(xcrun --show-sdk-path 2>/dev/null || true)
    if [ -n "$SDK" ]; then
        TIDY_ARGS+=(--extra-arg=-isysroot "--extra-arg=$SDK")
        SYSROOT_NOTE=" [sysroot $SDK]"
    else
        echo "clang-tidy.sh: xcrun found no SDK - results on macOS will be unreliable." >&2
    fi
fi

echo "[clang-tidy] $(echo "$FILES" | grep -c .) files ($(command -v "$CLANG_TIDY"))$SYSROOT_NOTE"
echo "$FILES" | xargs "$CLANG_TIDY" "${TIDY_ARGS[@]}"
