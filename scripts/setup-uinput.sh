#!/usr/bin/env bash
# Grants write access to /dev/uinput so Deskhub can inject mouse/keyboard.
# The deb/rpm packages do this on install; this script is for the portable
# binary and for source checkouts ('make setup-linux-permissions' calls it).
# Portable-binary users run it without a checkout:
#   curl -fsSL https://raw.githubusercontent.com/manhpham90vn/Deskhub/main/scripts/setup-uinput.sh | sudo bash
# Keep the rule line identical to the one scripts/stage-linux-pkgroot.sh
# puts into the packages.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "setup-uinput.sh: must run as root - re-run with sudo." >&2
    exit 1
fi

echo 'KERNEL=="uinput", SUBSYSTEM=="misc", MODE="0660", GROUP="input", TAG+="uaccess", OPTIONS+="static_node=uinput"' \
    > /etc/udev/rules.d/60-deskhub-uinput.rules
echo uinput > /etc/modules-load.d/deskhub.conf

udevadm control --reload-rules
modprobe uinput 2>/dev/null || true
udevadm trigger --name-match=uinput

if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ]; then
    usermod -aG input "$SUDO_USER"
fi

echo "setup-uinput.sh: done - the desktop session works right away; ssh/headless sessions need a log-out/in for the 'input' group."
