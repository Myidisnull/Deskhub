#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

PREFIX="$PWD/third_party/opus"
HOST_OS="$(uname -s)"

case "$HOST_OS" in
MINGW* | MSYS* | CYGWIN*) TARGET=x86_64-pc-windows-msvc ;;
Darwin)
    case "$(uname -m)" in
    arm64) TARGET=aarch64-apple-darwin ;;
    *)     TARGET=x86_64-apple-darwin ;;
    esac
    ;;
*)
    case "$(uname -m)" in
    aarch64 | arm64) TARGET=aarch64-unknown-linux-gnu ;;
    *)               TARGET=x86_64-unknown-linux-gnu ;;
    esac
    ;;
esac

case "$TARGET" in
*-windows-msvc) OPUS_LIB="$PREFIX/$TARGET/opus.lib" ;;
*)              OPUS_LIB="$PREFIX/$TARGET/libopus.a" ;;
esac

[ -f "$OPUS_LIB" ] || {
    echo "opus-smoke.sh: $OPUS_LIB is missing - run scripts/build-opus.sh first." >&2
    exit 1
}

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

case "$HOST_OS" in
MINGW* | MSYS* | CYGWIN*)
    command -v cl >/dev/null 2>&1 || {
        echo "opus-smoke.sh: MSVC 'cl.exe' is not on PATH." >&2
        echo "               Open 'x64 Native Tools Command Prompt for VS' (or run vcvars64.bat)," >&2
        echo "               then start bash from it and re-run this script." >&2
        exit 1
    }
    PATH="$(dirname "$(command -v cl)"):$PATH"
    WINWORK="$(cygpath -w "$WORK")"
    MSYS2_ARG_CONV_EXCL='*' cl /nologo /std:c++20 /W4 /EHsc /O2 /MT \
        /I"$(cygpath -w "$PREFIX/include")" \
        "$(cygpath -w "$PWD/scripts/opus-smoke.cpp")" \
        /Fe:"$WINWORK\\opus-smoke.exe" /Fo:"$WINWORK\\opus-smoke.obj" \
        /link "$(cygpath -w "$OPUS_LIB")"
    ;;
*)
    c++ -std=c++20 -Wall -Wextra -O2 scripts/opus-smoke.cpp \
        -I"$PREFIX/include" -o "$WORK/opus-smoke" "$OPUS_LIB" -lm
    ;;
esac

"$WORK/opus-smoke"
