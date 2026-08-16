#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

PREFIX="$PWD/third_party/quiche"
TARGET="$(rustc -vV | awk '/^host:/ { print $2 }')"
case "$TARGET" in
*-windows-msvc) QUICHE_LIB="$PREFIX/$TARGET/quiche.lib" ;;
*)              QUICHE_LIB="$PREFIX/$TARGET/libquiche.a" ;;
esac
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[ -f "$QUICHE_LIB" ] || {
    echo "quic-smoke.sh: $QUICHE_LIB is missing - run scripts/build-quiche.sh first." >&2
    exit 1
}

command -v openssl >/dev/null 2>&1 || {
    echo "quic-smoke.sh: 'openssl' is required to generate the test certificate." >&2
    exit 1
}

HOST_OS="$(uname -s)"

case "$HOST_OS" in
MINGW* | MSYS* | CYGWIN*) SUBJECT='//CN=deskhub-host' ;;
*)                        SUBJECT='/CN=deskhub-host' ;;
esac

openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
    -subj "$SUBJECT" \
    -keyout "$WORK/cert.key" -out "$WORK/cert.crt" 2>/dev/null

CERTDIR="$WORK"

case "$HOST_OS" in
MINGW* | MSYS* | CYGWIN*)
    command -v cl >/dev/null 2>&1 || {
        echo "quic-smoke.sh: MSVC 'cl.exe' is not on PATH." >&2
        echo "               Open 'x64 Native Tools Command Prompt for VS' (or run vcvars64.bat)," >&2
        echo "               then start bash from it and re-run this script." >&2
        exit 1
    }
    PATH="$(dirname "$(command -v cl)"):$PATH"
    WINWORK="$(cygpath -w "$WORK")"
    MSYS2_ARG_CONV_EXCL='*' cl /nologo /std:c++20 /W4 /EHsc /O2 /MT \
        /I"$(cygpath -w "$PREFIX/include")" \
        "$(cygpath -w "$PWD/scripts/quic-smoke.cpp")" \
        /Fe:"$WINWORK\\quic-smoke.exe" /Fo:"$WINWORK\\quic-smoke.obj" \
        /link "$(cygpath -w "$QUICHE_LIB")" \
        ws2_32.lib userenv.lib advapi32.lib bcrypt.lib ntdll.lib crypt32.lib
    CERTDIR="$WINWORK"
    ;;
Darwin)
    c++ -std=c++20 -Wall -Wextra -O2 scripts/quic-smoke.cpp \
        -I"$PREFIX/include" -o "$WORK/quic-smoke" "$QUICHE_LIB" \
        -framework Security -framework CoreFoundation
    ;;
*)
    c++ -std=c++20 -Wall -Wextra -O2 scripts/quic-smoke.cpp \
        -I"$PREFIX/include" -o "$WORK/quic-smoke" "$QUICHE_LIB" -lpthread -ldl -lm
    ;;
esac

"$WORK/quic-smoke" "$CERTDIR"
