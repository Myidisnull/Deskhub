#!/usr/bin/env bash
# Stages the distro-neutral payload both linux packages share (deb + rpm):
# binary, desktop entry, icons, udev rule, modules-load entry, license docs.
set -euo pipefail
cd "$(dirname "$0")/.."

DEST="${1:?usage: stage-linux-pkgroot.sh <dest-dir>}"
BIN=out/build/x64-release/client/linux/deskhub

[ -f "$BIN" ] || {
    echo "stage-linux-pkgroot.sh: $BIN not found - run 'make release-linux' first." >&2
    exit 1
}

rm -rf "$DEST"

install -Dm755 "$BIN" "$DEST/usr/bin/deskhub"
strip "$DEST/usr/bin/deskhub"

install -Dm644 client/macos/app/Assets.xcassets/AppIcon.appiconset/icon_256.png \
    "$DEST/usr/share/icons/hicolor/256x256/apps/deskhub.png"
install -Dm644 client/macos/app/Assets.xcassets/AppIcon.appiconset/icon_512.png \
    "$DEST/usr/share/icons/hicolor/512x512/apps/deskhub.png"

mkdir -p "$DEST/usr/share/applications"
cat > "$DEST/usr/share/applications/deskhub.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Deskhub
Comment=Share and control desktops over the local network
Exec=deskhub
Icon=deskhub
Terminal=false
Categories=Network;RemoteAccess;
EOF

# 60- sorts before systemd's 73-seat-late.rules, which is what turns the
# uaccess tag into an ACL for the user at the active seat (no group, no
# re-login). GROUP="input" stays as the fallback for headless sessions.
mkdir -p "$DEST/usr/lib/udev/rules.d"
cat > "$DEST/usr/lib/udev/rules.d/60-deskhub-uinput.rules" <<'EOF'
KERNEL=="uinput", SUBSYSTEM=="misc", MODE="0660", GROUP="input", TAG+="uaccess", OPTIONS+="static_node=uinput"
EOF

mkdir -p "$DEST/usr/lib/modules-load.d"
echo uinput > "$DEST/usr/lib/modules-load.d/deskhub.conf"

mkdir -p "$DEST/usr/share/doc/deskhub"
{
    echo "Deskhub is distributed under the MIT License."
    echo "Third-party components and their licenses: see THIRD_PARTY_NOTICES.md."
    echo
    cat LICENSE
} > "$DEST/usr/share/doc/deskhub/copyright"
install -Dm644 THIRD_PARTY_NOTICES.md "$DEST/usr/share/doc/deskhub/THIRD_PARTY_NOTICES.md"
install -Dm644 licenses/LGPL-2.1.txt "$DEST/usr/share/doc/deskhub/LGPL-2.1.txt"
