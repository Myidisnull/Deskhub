#!/usr/bin/env bash
set -euo pipefail

APT_TIMEOUT=${APT_TIMEOUT:-300}
APT_RETRIES=${APT_RETRIES:-3}

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

for attempt in $(seq 1 "$APT_RETRIES"); do
    if timeout "$APT_TIMEOUT" sudo DEBIAN_FRONTEND=noninteractive apt-get update -qq &&
        timeout "$APT_TIMEOUT" sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"; then
        exit 0
    fi
    echo "apt-install.sh: attempt $attempt of $APT_RETRIES failed or exceeded ${APT_TIMEOUT}s." >&2
    sleep $((attempt * 5))
done

echo "apt-install.sh: giving up on ${missing[*]} after $APT_RETRIES attempts." >&2
echo "apt-install.sh: a bare 'apt-get update' can hang indefinitely on a stalled mirror," >&2
echo "apt-install.sh: which burns the whole job timeout instead of failing here." >&2
exit 1
