#!/usr/bin/env bash
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
