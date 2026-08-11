#!/usr/bin/env bash
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
