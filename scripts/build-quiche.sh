#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

QUICHE_VERSION=0.29.3
QUICHE_COMMIT=55886df3be579579207104c8e645825b6347a209
PREFIX="$PWD/third_party/quiche"
SRC="$PREFIX/src"

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

export_boringssl_headers() {
    local cargo_home="${CARGO_HOME:-$HOME/.cargo}"
    local found=""
    if [ -d "$cargo_home/registry/src" ]; then
        found=$(find "$cargo_home/registry/src" -maxdepth 6 -type d \
            -path '*/boring-sys-*/deps/boringssl/src/include' 2>/dev/null | sort | tail -1) ||
            true
    fi
    if [ -z "$found" ]; then
        if [ -d "$PREFIX/include/openssl" ]; then
            echo "[ok]      BoringSSL headers already exported"
            return 0
        fi
        echo "build-quiche.sh: BoringSSL headers not found under $cargo_home/registry/src." >&2
        echo "                 Without include/openssl the CMake build silently falls back to" >&2
        echo "                 the no-QUIC stubs and the apps cannot share. They appear once" >&2
        echo "                 cargo has fetched boring-sys; re-run this script after a build." >&2
        return 1
    fi
    rm -rf "$PREFIX/include/openssl"
    cp -R "$found/openssl" "$PREFIX/include/openssl"
    echo "[ok]      BoringSSL headers → third_party/quiche/include/openssl"
}

is_msvc_target() {
    case "$1" in
        *-windows-msvc) return 0 ;;
        *) return 1 ;;
    esac
}

prefer_msvc_tools() {
    local cl
    cl=$(command -v cl 2>/dev/null) || cl=""
    [ -n "$cl" ] || {
        echo "build-quiche.sh: MSVC 'cl.exe' is not on PATH." >&2
        echo "                 Open 'x64 Native Tools Command Prompt for VS' (or run vcvars64.bat)," >&2
        echo "                 then start bash from it and re-run this script." >&2
        exit 1
    }
    PATH="$(dirname "$cl"):$PATH"
    export PATH
}

prefer_nasm() {
    command -v nasm >/dev/null 2>&1 && return 0
    local nasm_dir="${LOCALAPPDATA:-}/bin/NASM"
    [ -x "$nasm_dir/nasm.exe" ] || return 0
    PATH="$nasm_dir:$PATH"
    export PATH
}

prefer_ninja_generator() {
    [ -n "${CMAKE_GENERATOR:-}" ] && return 0
    local vs_cmake="${VSINSTALLDIR:-}/Common7/IDE/CommonExtensions/Microsoft/CMake"
    [ -n "${VSINSTALLDIR:-}" ] && [ -d "$vs_cmake" ] &&
        PATH="$vs_cmake/CMake/bin:$vs_cmake/Ninja:$PATH" && export PATH
    command -v ninja >/dev/null 2>&1 || return 0
    export CMAKE_GENERATOR=Ninja
}

refuse_crt_rustflags() {
    case "${RUSTFLAGS:-}" in
        *crt-static*)
            echo "build-quiche.sh: RUSTFLAGS overrides crt-static; unset it." >&2
            echo "                 The msvc default is already the static CRT the CMake" >&2
            echo "                 tree links against, and forcing it through RUSTFLAGS" >&2
            echo "                 leaks into proc-macros and kills cargo with exit 101." >&2
            exit 1
            ;;
    esac
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
    local artifact want
    artifact=$(artifact_of "$target")
    want="$QUICHE_COMMIT"
    is_msvc_target "$target" && want="$QUICHE_COMMIT+crt-static"

    if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$want" ] && [ -f "$out/$artifact" ]; then
        echo "[ok]      quiche $QUICHE_VERSION ($target)"
        return 0
    fi

    rustup target add "$target" >/dev/null 2>&1 || true
    if is_msvc_target "$target"; then
        prefer_msvc_tools
        prefer_nasm
        prefer_ninja_generator
        refuse_crt_rustflags
    fi

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
    echo "$want" >"$stamp"
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

make_macos_universal() {
    [ "$(uname -s)" = "Darwin" ] || return 0
    local arm="$PREFIX/aarch64-apple-darwin/libquiche.a"
    local x64="$PREFIX/x86_64-apple-darwin/libquiche.a"
    [ -f "$arm" ] && [ -f "$x64" ] || return 0
    mkdir -p "$PREFIX/macos-universal"
    lipo -create "$arm" "$x64" -output "$PREFIX/macos-universal/libquiche.a"
    echo "[ok]      quiche $QUICHE_VERSION (macos-universal) = arm64 + x86_64"
}
make_macos_universal

export_boringssl_headers
