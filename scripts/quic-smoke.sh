#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# M0 acceptance check: proves the quiche build on this machine can complete a TLS
# handshake, carry a reliable stream (the terminal path) and carry a datagram (the
# video path), all from C++. Run scripts/build-quiche.sh first.

PREFIX="$PWD/third_party/quiche"
TARGET="$(rustc -vV | awk '/^host:/ { print $2 }')"
LIB="$PREFIX/$TARGET/libquiche.a"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[ -f "$LIB" ] || {
    echo "quic-smoke.sh: $LIB is missing - run scripts/build-quiche.sh first." >&2
    exit 1
}

command -v openssl >/dev/null 2>&1 || {
    echo "quic-smoke.sh: 'openssl' is required to generate the test certificate." >&2
    exit 1
}

openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
    -subj '/CN=deskhub-host' \
    -keyout "$WORK/cert.key" -out "$WORK/cert.crt" 2>/dev/null

case "$(uname -s)" in
Darwin) SYSLIBS=(-framework Security -framework CoreFoundation) ;;
*)      SYSLIBS=(-lpthread -ldl -lm) ;;
esac

c++ -std=c++20 -Wall -Wextra -O2 scripts/quic-smoke.cpp \
    -I"$PREFIX/include" -o "$WORK/quic-smoke" "$LIB" "${SYSLIBS[@]}"

"$WORK/quic-smoke" "$WORK"
