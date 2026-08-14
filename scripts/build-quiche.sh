#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

QUICHE_VERSION=0.29.3
QUICHE_COMMIT=55886df3be579579207104c8e645825b6347a209
PREFIX="$PWD/third_party/quiche"
SRC="$PREFIX/src"

# Usage: build-quiche.sh [group ...]
#   host     native target of this machine (default)
#   android  arm64-v8a, armeabi-v7a, x86_64  (needs ANDROID_NDK_HOME + cargo-ndk)
#   apple    macOS arm64/x64, iOS device, iOS simulator  (macOS host only)
#   windows  x86_64-pc-windows-msvc  (Windows host only)
# Each target lands in third_party/quiche/<rust-target>/libquiche.a,
# the shared header in third_party/quiche/include/quiche.h.

ANDROID_TARGETS=(aarch64-linux-android armv7-linux-androideabi x86_64-linux-android)
APPLE_TARGETS=(aarch64-apple-darwin x86_64-apple-darwin aarch64-apple-ios aarch64-apple-ios-sim)
WINDOWS_TARGETS=(x86_64-pc-windows-msvc)

command -v cargo >/dev/null 2>&1 || {
    echo "build-quiche.sh: 'cargo' is required. Install Rust: https://rustup.rs" >&2
    exit 1
}

host_target() {
    rustc -vV | awk '/^host:/ { print $2 }'
}

resolve_targets() {
    local group targets=()
    for group in "$@"; do
        case "$group" in
            host) targets+=("$(host_target)") ;;
            android) targets+=("${ANDROID_TARGETS[@]}") ;;
            apple) targets+=("${APPLE_TARGETS[@]}") ;;
            windows) targets+=("${WINDOWS_TARGETS[@]}") ;;
            *) targets+=("$group") ;;
        esac
    done
    printf '%s\n' "${targets[@]}"
}

fetch_source() {
    [ -f "$SRC/.commit" ] && [ "$(cat "$SRC/.commit")" = "$QUICHE_COMMIT" ] && return 0
    echo "[fetch]   quiche $QUICHE_VERSION ($QUICHE_COMMIT)"
    rm -rf "$SRC"
    mkdir -p "$SRC"
    git clone --quiet --depth 1 --branch "$QUICHE_VERSION" \
        https://github.com/cloudflare/quiche "$SRC"
    local got
    got=$(git -C "$SRC" rev-parse HEAD)
    if [ "$got" != "$QUICHE_COMMIT" ]; then
        echo "build-quiche.sh: tag $QUICHE_VERSION resolved to $got, expected $QUICHE_COMMIT." >&2
        rm -rf "$SRC"
        exit 1
    fi
    echo "$QUICHE_COMMIT" >"$SRC/.commit"
}

is_android_target() {
    case "$1" in
        *-linux-android | *-linux-androideabi) return 0 ;;
        *) return 1 ;;
    esac
}

android_abi_of() {
    case "$1" in
        aarch64-linux-android) echo arm64-v8a ;;
        armv7-linux-androideabi) echo armeabi-v7a ;;
        x86_64-linux-android) echo x86_64 ;;
        i686-linux-android) echo x86 ;;
    esac
}

# Rust names a staticlib after the platform's own convention: libquiche.a
# everywhere, but quiche.lib on MSVC. Keep the native name so the linker on
# each platform sees what it expects.
artifact_of() {
    case "$1" in
        *-windows-msvc) echo quiche.lib ;;
        *) echo libquiche.a ;;
    esac
}

build_target() {
    local target=$1
    local out="$PREFIX/$target"
    local stamp="$out/.stamp"
    local artifact
    artifact=$(artifact_of "$target")

    if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$QUICHE_COMMIT" ] && [ -f "$out/$artifact" ]; then
        echo "[ok]      quiche $QUICHE_VERSION ($target)"
        return 0
    fi

    rustup target add "$target" >/dev/null 2>&1 || true

    echo "[build]   quiche $QUICHE_VERSION ($target)..."
    if is_android_target "$target"; then
        command -v cargo-ndk >/dev/null 2>&1 || {
            echo "build-quiche.sh: 'cargo-ndk' is required for Android targets." >&2
            echo "                 cargo install cargo-ndk" >&2
            exit 1
        }
        [ -n "${ANDROID_NDK_HOME:-}" ] || {
            echo "build-quiche.sh: ANDROID_NDK_HOME is not set." >&2
            exit 1
        }
        (cd "$SRC" && cargo ndk --target "$(android_abi_of "$target")" --platform 24 \
            -- build --release -p quiche --features ffi >/dev/null)
    else
        (
            cd "$SRC"
            case "$target" in
            *-apple-ios*)
                # Pin the minimum iOS version for both compilers. Without this,
                # boring-sys's clang floats to the SDK default while rustc links
                # for its own minimum, and the mismatch surfaces as an undefined
                # ___chkstk_darwin at link time. 17.0 matches the iOS app.
                export IPHONEOS_DEPLOYMENT_TARGET="${IPHONEOS_DEPLOYMENT_TARGET:-17.0}"
                ;;
            esac
            cargo build --release --target "$target" -p quiche --features ffi >/dev/null
        )
    fi

    local built="$SRC/target/$target/release/$artifact"
    [ -f "$built" ] || {
        echo "build-quiche.sh: cargo reported success but $built is missing." >&2
        echo "                 Built artifacts for $target:" >&2
        ls "$SRC/target/$target/release/" 2>/dev/null | sed 's/^/                   /' >&2
        exit 1
    }

    mkdir -p "$out"
    cp "$built" "$out/$artifact"
    echo "$QUICHE_COMMIT" >"$stamp"
    echo "[ok]      quiche $QUICHE_VERSION ($target) → third_party/quiche/$target/$artifact"
}

REQUESTED=("$@")
[ ${#REQUESTED[@]} -gt 0 ] || REQUESTED=(host)

TARGETS=()
while IFS= read -r target; do
    TARGETS+=("$target")
done < <(resolve_targets "${REQUESTED[@]}")

fetch_source
mkdir -p "$PREFIX/include"
cp "$SRC/quiche/include/quiche.h" "$PREFIX/include/quiche.h"

for target in "${TARGETS[@]}"; do
    build_target "$target"
done
