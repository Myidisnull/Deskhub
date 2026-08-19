#!/usr/bin/env bash
set -euo pipefail

APT_UPDATE_TIMEOUT=${APT_UPDATE_TIMEOUT:-90}
APT_INSTALL_TIMEOUT=${APT_INSTALL_TIMEOUT:-240}
APT_RETRIES=${APT_RETRIES:-3}

APT_OPTS=(-o Acquire::Retries=1 -o Acquire::http::Timeout=15 -o Acquire::https::Timeout=15)

declare -a missing=()
for pkg in "$@"; do
    case "$pkg" in
    ninja-build) cmd=ninja ;;
    clang) cmd=clang ;;
    llvm) cmd=llvm-cov ;;
    shellcheck) cmd=shellcheck ;;
    rpm) cmd=rpmbuild ;;
    *) cmd=$pkg ;;
    esac
    command -v "$cmd" >/dev/null 2>&1 || missing+=("$pkg")
done

if [ ${#missing[@]} -eq 0 ]; then
    echo "apt-install.sh: already present, skipping apt: $*"
    exit 0
fi

echo "apt-install.sh: installing ${missing[*]}"

fall_back_to_the_main_mirror() {
    local list switched=0
    for list in /etc/apt/sources.list /etc/apt/sources.list.d/*.list \
        /etc/apt/sources.list.d/*.sources; do
        [ -f "$list" ] || continue
        if grep -q 'azure\.archive\.ubuntu\.com' "$list"; then
            sudo sed -i 's|://azure\.archive\.ubuntu\.com|://archive.ubuntu.com|g' "$list"
            switched=1
        fi
    done
    if [ "$switched" -eq 1 ]; then
        echo "apt-install.sh: retrying against archive.ubuntu.com; the runner's default" >&2
        echo "apt-install.sh: azure mirror was not answering, and asking it again is what" >&2
        echo "apt-install.sh: turns a 20-second step into a twenty-minute one." >&2
    fi
}

for attempt in $(seq 1 "$APT_RETRIES"); do
    if timeout "$APT_UPDATE_TIMEOUT" sudo DEBIAN_FRONTEND=noninteractive \
        apt-get "${APT_OPTS[@]}" update -qq &&
        timeout "$APT_INSTALL_TIMEOUT" sudo DEBIAN_FRONTEND=noninteractive \
            apt-get "${APT_OPTS[@]}" install -y --no-install-recommends "${missing[@]}"; then
        exit 0
    fi
    echo "apt-install.sh: attempt $attempt of $APT_RETRIES failed or ran past its timeout." >&2
    if [ "$attempt" -eq 1 ]; then
        fall_back_to_the_main_mirror
    fi
    sleep $((attempt * 5))
done

echo "apt-install.sh: giving up on ${missing[*]} after $APT_RETRIES attempts." >&2
echo "apt-install.sh: apt is told to time out its own transfers in ${APT_OPTS[*]}; without" >&2
echo "apt-install.sh: that a stalled mirror hangs until the outer timeout, and three of" >&2
echo "apt-install.sh: those in a row cost more than the whole build normally takes." >&2
exit 1
