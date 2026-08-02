#!/usr/bin/env bash
# Push the three cross-compiled test binaries onto a booted emulator and run
# them. This lives in a script rather than inline in tests.yml because the
# emulator action feeds its `script:` input to the shell one line at a time,
# which breaks any multi-line construct such as a for loop.
#
# Usage: scripts/run-android-tests.sh <abi>
set -euo pipefail
cd "$(dirname "$0")/.."

abi="${1:?usage: run-android-tests.sh <abi>}"
build="out/build/android-${abi}"
device_dir=/data/local/tmp/deskhub

adb wait-for-device
adb shell mkdir -p "$device_dir"

log=$(mktemp)
trap 'rm -f "$log"' EXIT

for t in core_tests platform_tests integration_tests; do
    case "$t" in
        core_tests)        src="$build/core/$t" ;;
        platform_tests)    src="$build/platform/$t" ;;
        integration_tests) src="$build/tests/integration/$t" ;;
    esac

    adb push "$src" "$device_dir/$t"
    adb shell chmod 755 "$device_dir/$t"

    echo "===== $t on Android ($abi) ====="
    adb shell "cd $device_dir && ./$t; echo EXIT=\$?" | tee "$log"
    if ! tr -d '\r' <"$log" | grep -qx "EXIT=0"; then
        echo "::error::$t failed on Android $abi"
        exit 1
    fi
done
