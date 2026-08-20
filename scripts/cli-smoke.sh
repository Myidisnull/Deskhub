#!/usr/bin/env bash
set -euo pipefail

CLI=${1:-out/build/x64-debug/client/cli/deskhub-cli}
PORT=${DESKHUB_SMOKE_PORT:-47989}
PASSCODE=0417
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

if [ ! -x "$CLI" ]; then
    echo "cli-smoke: $CLI is not there - run 'make build-cli' first" >&2
    exit 1
fi

fail() {
    echo "::error::cli-smoke: $1" >&2
    exit 1
}

expect_code() {
    local want=$1
    shift
    local got=0
    "$@" >"$WORK/out" 2>"$WORK/err" || got=$?
    if [ "$got" != "$want" ]; then
        echo "--- stdout"; cat "$WORK/out"
        echo "--- stderr"; cat "$WORK/err"
        fail "'$*' exited $got, expected $want"
    fi
}

echo "== version and help"
expect_code 0 "$CLI" version
expect_code 0 "$CLI" help
expect_code 0 "$CLI" help share
expect_code 2 "$CLI" nonsense
expect_code 2 "$CLI" sources
expect_code 2 "$CLI" share --fps 0

echo "== stores read without a host"
expect_code 0 "$CLI" devices --json
expect_code 0 "$CLI" trust --json
expect_code 0 "$CLI" settings --json

echo "== nobody is listening"
expect_code 3 "$CLI" probe "127.0.0.1:$((PORT + 1))"

case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
        echo "== the rest needs POSIX signals - skipping on Windows"
        echo "cli-smoke: OK"
        exit 0
        ;;
esac

echo "== a shell-only host, and a viewer that talks to it"
"$CLI" share --no-screen --terminal --port "$PORT" --passcode "$PASSCODE" --quiet \
    >"$WORK/share.out" 2>"$WORK/share.err" &
SHARE_PID=$!

ready=0
for _ in $(seq 1 200); do
    if grep -q "Listening on UDP" "$WORK/share.err" 2>/dev/null; then
        ready=1
        break
    fi
    if ! kill -0 "$SHARE_PID" 2>/dev/null; then
        break
    fi
    sleep 0.1
done
if [ "$ready" != 1 ]; then
    cat "$WORK/share.err"
    fail "the host never started listening"
fi

expect_code 0 "$CLI" probe "127.0.0.1:$PORT"
expect_code 0 "$CLI" sources "127.0.0.1:$PORT" --passcode "$PASSCODE" --json
grep -q '"terminal":true' "$WORK/out" || fail "the host did not offer a shell"

expect_code 4 "$CLI" sources "127.0.0.1:$PORT" --passcode 9999

printf 'echo deskhub-smoke-ok\nexit\n' |
    "$CLI" shell "127.0.0.1:$PORT" --passcode "$PASSCODE" >"$WORK/shell.out" 2>/dev/null || true
tr -d '\r' <"$WORK/shell.out" | grep -aq "deskhub-smoke-ok" ||
    fail "the remote shell did not run the command"

kill -INT "$SHARE_PID"
wait "$SHARE_PID" || fail "the host did not stop cleanly on an interrupt"

echo "cli-smoke: OK"
