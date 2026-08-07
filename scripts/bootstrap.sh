#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

CLANG_FORMAT_VERSION=22.1.3
KTLINT_VERSION=1.5.0
KTLINT_SHA256=a16be01dcc480aab2f55f444b620142152f66e31564b3b9376506d624c28a2ad
SWIFTFORMAT_VERSION=0.62.1
SWIFTFORMAT_MACOS_SHA256=7cb1cb1fae04932047c7015441c543848e8e60e1572d808d080e0a1f1661114a
SWIFTFORMAT_LINUX_SHA256=61ff55f3581e2144a4ad114831167102c38be853df75c1477d20b40a8e8120aa

have() { command -v "$1" >/dev/null 2>&1; }

sync_submodules() {
    [ -e .git ] || return 0
    if git submodule status | grep -q '^-'; then
        echo "[install] git submodules (nvenc headers)..."
        git submodule update --init
    else
        echo "[ok]      git submodules"
    fi
}

verify_sha256() {
    if have sha256sum; then
        echo "$2  $1" | sha256sum --check --status -
    else
        echo "$2  $1" | shasum -a 256 --check --status -
    fi
}

install_clang_format() {
    if have clang-format && clang-format --version | grep -qF "$CLANG_FORMAT_VERSION"; then
        echo "[ok]      clang-format $CLANG_FORMAT_VERSION"
    else
        echo "[install] clang-format $CLANG_FORMAT_VERSION (pipx)..."
        pipx install --force "clang-format==$CLANG_FORMAT_VERSION"
        pipx ensurepath
    fi
}

install_format_tools() {
    mkdir -p tools

    if [ -f tools/ktlint.jar ] && [ "$(cat tools/ktlint.jar.version 2>/dev/null)" = "$KTLINT_VERSION" ]; then
        echo "[ok]      ktlint $KTLINT_VERSION (tools/ktlint.jar)"
    else
        echo "[install] ktlint $KTLINT_VERSION..."
        curl -fsSL -o tools/ktlint.jar "https://github.com/pinterest/ktlint/releases/download/$KTLINT_VERSION/ktlint"
        verify_sha256 tools/ktlint.jar "$KTLINT_SHA256" || {
            echo "bootstrap: ktlint download failed the checksum check." >&2
            rm -f tools/ktlint.jar tools/ktlint.jar.version
            exit 1
        }
        echo "$KTLINT_VERSION" >tools/ktlint.jar.version
    fi

    if have swiftformat; then
        echo "[ok]      swiftformat ($(command -v swiftformat))"
    elif [ -x tools/swiftformat ] && [ "$(tools/swiftformat --version 2>/dev/null)" = "$SWIFTFORMAT_VERSION" ]; then
        echo "[ok]      swiftformat $SWIFTFORMAT_VERSION (tools/swiftformat)"
    else
        echo "[install] SwiftFormat $SWIFTFORMAT_VERSION..."
        case "$(uname -s)" in
        Darwin) ASSET=swiftformat.zip       BIN=swiftformat       SHA=$SWIFTFORMAT_MACOS_SHA256 ;;
        *)      ASSET=swiftformat_linux.zip BIN=swiftformat_linux SHA=$SWIFTFORMAT_LINUX_SHA256 ;;
        esac
        curl -fsSL -o tools/swiftformat.zip "https://github.com/nicklockwood/SwiftFormat/releases/download/$SWIFTFORMAT_VERSION/$ASSET"
        verify_sha256 tools/swiftformat.zip "$SHA" || {
            echo "bootstrap: SwiftFormat download failed the checksum check." >&2
            rm -f tools/swiftformat.zip
            exit 1
        }
        unzip -o -q -d tools tools/swiftformat.zip "$BIN"
        if [ "$BIN" != swiftformat ]; then mv "tools/$BIN" tools/swiftformat; fi
        chmod +x tools/swiftformat
        rm -f tools/swiftformat.zip
    fi
}

install_android_packages() {
    SDK="${ANDROID_HOME:-$1}"
    SDKMANAGER=""
    for cand in "$SDK"/cmdline-tools/*/bin/sdkmanager; do
        if [ -x "$cand" ]; then SDKMANAGER="$cand"; break; fi
    done
    if [ -n "$SDKMANAGER" ]; then
        echo "[ok]      Android SDK ($SDK)"
        if [ -d "$SDK/platform-tools" ] && [ -d "$SDK/platforms/android-37.0" ] &&
           [ -d "$SDK/ndk/26.1.10909125" ] && [ -d "$SDK/cmake/3.22.1" ]; then
            echo "[ok]      Android SDK packages (platform 37.0, NDK 26.1.10909125, cmake 3.22.1)"
            return
        fi
        echo "[install] SDK packages (platform 37.0, NDK 26.1.10909125, cmake 3.22.1)..."
        "$SDKMANAGER" --install 'platform-tools' 'platforms;android-37.0' 'ndk;26.1.10909125' 'cmake;3.22.1'
    else
        echo "[action]  Android cmdline-tools missing - install Android Studio or sdkmanager, set ANDROID_HOME, then re-run bootstrap."
    fi
}

sync_submodules

case "$(uname -s)" in
Darwin)
    if xcode-select -p >/dev/null 2>&1; then
        echo "[ok]      Xcode command line tools ($(xcode-select -p))"
    else
        echo "[action]  Xcode missing - run 'xcode-select --install' (CLT) and install Xcode from the App Store for client/ios."
    fi

    have brew || { echo "Homebrew not found - install from https://brew.sh first." >&2; exit 1; }

    for pkg in cmake ninja swiftlint pipx; do
        if have "$pkg"; then
            echo "[ok]      $pkg ($(command -v "$pkg"))"
        else
            echo "[install] $pkg..."
            brew install "$pkg"
        fi
    done
    if have java; then
        echo "[ok]      java ($(command -v java))"
    else
        echo "[install] JDK 17 (Temurin)..."
        brew install --cask temurin@17
    fi

    install_clang_format
    install_format_tools
    install_android_packages "$HOME/Library/Android/sdk"
    ;;

Linux)
    have apt-get || { echo "Only Ubuntu/Debian (apt) is supported for now." >&2; exit 1; }

    echo "[install] apt packages (build-essential clang llvm cmake ninja-build openjdk-17-jdk-headless pipx unzip curl pkg-config rpm)..."
    sudo apt-get update -qq
    sudo apt-get install -y build-essential clang llvm cmake ninja-build openjdk-17-jdk-headless pipx unzip curl pkg-config rpm

    echo "[install] apt packages for the Ubuntu app (PipeWire, VA-API, GTK3, nasm)..."
    sudo apt-get install -y \
        libgtk-3-dev libglib2.0-dev libepoxy-dev libegl-dev libgles-dev \
        libdrm-dev libva-dev libpipewire-0.3-dev libspa-0.2-dev \
        nasm

    echo "[install] VA-API drivers + GNOME portal (KDE/wlroots users: install the matching xdg-desktop-portal backend)..."
    sudo apt-get install -y va-driver-all vainfo xdg-desktop-portal xdg-desktop-portal-gnome || true

    scripts/build-ffmpeg.sh

    install_clang_format
    install_format_tools
    install_android_packages "$HOME/Android/Sdk"
    ;;

*)
    echo "Unsupported OS: $(uname -s) - Windows uses scripts/bootstrap.ps1." >&2
    exit 1
    ;;
esac

echo ""
echo "bootstrap: DONE"
echo "  Next: 'make' (list every target), 'make test', 'make lint', 'make build-<os>'"
