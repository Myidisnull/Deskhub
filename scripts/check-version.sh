#!/usr/bin/env bash
# The store builds take their marketing version from VERSION, while the GitHub
# release is named after the tag. This makes a mismatch a build failure instead
# of shipping "v2.1.0" with 2.0.4 printed inside the app.
#
#   scripts/check-version.sh [ref]      ref defaults to $GITHUB_REF
set -euo pipefail
cd "$(dirname "$0")/.."

REF=${1:-${GITHUB_REF:-}}
VERSION=$(tr -d ' \t\r\n' <VERSION)

case "$REF" in
refs/tags/*) TAG=${REF#refs/tags/} ;;
v*) TAG=$REF ;;
*)
    echo "not a tag ref (${REF:-none}) - VERSION is $VERSION, nothing to check"
    exit 0
    ;;
esac

if [ "$TAG" != "v$VERSION" ]; then
    echo "::error::tag $TAG does not match VERSION ($VERSION) - expected tag v$VERSION"
    exit 1
fi

echo "tag $TAG matches VERSION $VERSION"
