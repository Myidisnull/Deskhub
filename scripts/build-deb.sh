#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

BIN=out/build/x64-release/client/linux/deskhub
[ -f "$BIN" ] || {
    echo "build-deb.sh: $BIN not found - run 'make release-linux' first." >&2
    exit 1
}

for tool in dpkg-deb dpkg-shlibdeps dpkg; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build-deb.sh: '$tool' is required (sudo apt install dpkg-dev)." >&2
        exit 1
    }
done

VERSION="$(tr -d '[:space:]' < VERSION)"
ARCH="$(dpkg --print-architecture)"
DIST=out/dist/linux
STAGE="$DIST/deb"
DEB="$DIST/deskhub_${VERSION}_${ARCH}.deb"

rm -rf "$STAGE" "$DEB"

install -Dm755 "$BIN" "$STAGE/usr/bin/deskhub"
strip "$STAGE/usr/bin/deskhub"

install -Dm644 client/macos/app/Assets.xcassets/AppIcon.appiconset/icon_256.png \
    "$STAGE/usr/share/icons/hicolor/256x256/apps/deskhub.png"
install -Dm644 client/macos/app/Assets.xcassets/AppIcon.appiconset/icon_512.png \
    "$STAGE/usr/share/icons/hicolor/512x512/apps/deskhub.png"

mkdir -p "$STAGE/usr/share/applications"
cat > "$STAGE/usr/share/applications/deskhub.desktop" <<'EOF'
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
mkdir -p "$STAGE/usr/lib/udev/rules.d"
cat > "$STAGE/usr/lib/udev/rules.d/60-deskhub-uinput.rules" <<'EOF'
KERNEL=="uinput", SUBSYSTEM=="misc", MODE="0660", GROUP="input", TAG+="uaccess", OPTIONS+="static_node=uinput"
EOF

mkdir -p "$STAGE/usr/lib/modules-load.d"
echo uinput > "$STAGE/usr/lib/modules-load.d/deskhub.conf"

mkdir -p "$STAGE/usr/share/doc/deskhub"
{
    echo "Deskhub is distributed under the MIT License."
    echo "Third-party components and their licenses: see THIRD_PARTY_NOTICES.md."
    echo
    cat LICENSE
} > "$STAGE/usr/share/doc/deskhub/copyright"
install -Dm644 THIRD_PARTY_NOTICES.md "$STAGE/usr/share/doc/deskhub/THIRD_PARTY_NOTICES.md"
install -Dm644 licenses/LGPL-2.1.txt "$STAGE/usr/share/doc/deskhub/LGPL-2.1.txt"

# dpkg-shlibdeps computes the exact runtime Depends from the binary's NEEDED
# entries; it insists on a debian/control stub existing next to the binary.
mkdir -p "$STAGE/debian"
touch "$STAGE/debian/control"
DEPENDS="$(cd "$STAGE" && dpkg-shlibdeps -O usr/bin/deskhub | sed 's/^shlibs:Depends=//')"
rm -rf "$STAGE/debian"

INSTALLED_SIZE="$(du -sk "$STAGE" | cut -f1)"

mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<EOF
Package: deskhub
Version: $VERSION
Architecture: $ARCH
Maintainer: Manh Pham <manhpv151090@gmail.com>
Section: net
Priority: optional
Installed-Size: $INSTALLED_SIZE
Depends: $DEPENDS
Recommends: xdg-desktop-portal, va-driver-all
Homepage: https://github.com/manhpham90vn/mp_remote
Description: LAN remote desktop - share and control screens
 Deskhub shares the screen of one machine and controls it from another
 over UDP on the local network.
 .
 This package contains the Ubuntu app (host + viewer role) and the udev
 rule that grants access to /dev/uinput, so remote mouse and keyboard
 input works without any manual permission setup.
EOF

cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    if command -v udevadm >/dev/null 2>&1; then
        udevadm control --reload-rules || true
        modprobe uinput 2>/dev/null || true
        udevadm trigger --name-match=uinput || true
    fi
fi
exit 0
EOF

cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    if command -v udevadm >/dev/null 2>&1; then
        udevadm control --reload-rules || true
    fi
fi
exit 0
EOF

chmod 755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

dpkg-deb --build --root-owner-group "$STAGE" "$DEB" >/dev/null
rm -rf "$STAGE"
echo "[ok]      $DEB"
