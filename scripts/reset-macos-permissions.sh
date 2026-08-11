#!/usr/bin/env bash
# Clear the macOS privacy grants for the Deskhub bundle id.
#
# TCC keys a grant by bundle id AND code signature. A locally built app.app is
# ad-hoc signed (its designated requirement changes on every rebuild) while the
# CI/DMG build is signed with Developer ID, so the two fight over the same
# com.deskhub.macos row: System Settings shows the permission as granted, yet the
# copy you just launched is denied - silently, for Accessibility.
#
# This drops every grant so the next launch asks again, and reports which copies
# of the app exist and how each one is signed.
set -euo pipefail
cd "$(dirname "$0")/.."

BUNDLE_ID=${DESKHUB_BUNDLE_ID:-com.deskhub.macos}
LSREGISTER=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
PURGE=0

usage() {
    cat <<EOF
usage: scripts/reset-macos-permissions.sh [--purge] [--bundle-id ID]

  --purge          also delete the local build output (out/build/macos,
                   out/dist/macos) so only one copy of the app is left.
                   Never touches /Applications.
  --bundle-id ID   default: $BUNDLE_ID

After it runs, relaunch the ONE copy you want to use and grant the prompts again.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
    --purge) PURGE=1 ;;
    --bundle-id)
        shift
        BUNDLE_ID=${1:?--bundle-id needs a value}
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "reset-macos-permissions.sh: unknown argument '$1'" >&2
        usage >&2
        exit 1
        ;;
    esac
    shift
done

if [ "$(uname)" != Darwin ]; then
    echo "reset-macos-permissions.sh: macOS only." >&2
    exit 1
fi

# The Xcode target is called `app`, so the executable inside the bundle is
# literally Contents/MacOS/app whatever the bundle got renamed to - match on the
# bundle path, never on the bare process name.
echo "==> quitting $BUNDLE_ID"
osascript -e "quit app id \"$BUNDLE_ID\"" 2>/dev/null || true
pkill -f '/(app|Deskhub)\.app/Contents/MacOS/' 2>/dev/null || true

echo "==> resetting privacy grants"
for service in Accessibility ScreenCapture ListenEvent PostEvent Microphone Camera; do
    if tccutil reset "$service" "$BUNDLE_ID" >/dev/null 2>&1; then
        echo "    $service: cleared"
    else
        echo "    $service: nothing to clear"
    fi
done
tccutil reset All "$BUNDLE_ID" >/dev/null 2>&1 || true

echo "==> installed copies known to Launch Services"
COPIES=$(mdfind "kMDItemCFBundleIdentifier == '$BUNDLE_ID'" 2>/dev/null || true)
if [ -z "$COPIES" ]; then
    echo "    none found - Spotlight may not have indexed out/, that is fine"
else
    while IFS= read -r app; do
        [ -n "$app" ] || continue
        info=$(codesign -dv --verbose=2 "$app" 2>&1 || true)
        authority=$(awk -F'=' '/^Authority=/ {print $2; exit}' <<<"$info")
        if [ -z "$authority" ]; then
            case "$info" in
            *Signature=adhoc*) authority="ad-hoc" ;;
            esac
        fi
        echo "    ${app}  [${authority:-unsigned}]"
    done <<<"$COPIES"
fi

if [ "$PURGE" -eq 1 ]; then
    echo "==> purging local build output"
    for stale in out/build/macos out/dist/macos; do
        [ -e "$stale" ] || continue
        find "$stale" -maxdepth 3 -name '*.app' -print0 2>/dev/null |
            xargs -0 -I{} "$LSREGISTER" -u "{}" 2>/dev/null || true
        rm -rf "$stale"
        echo "    removed $stale"
    done
fi

cat <<EOF

==> done
Keep ONE copy of the app around - either the local build or the downloaded one.
Launch it, then grant Screen Recording and Accessibility when it asks; both
permissions only take effect for the exact binary that asked for them.

If peer discovery still fails, toggle the app off/on under
System Settings > Privacy & Security > Local Network - tccutil cannot reset that one.
EOF
