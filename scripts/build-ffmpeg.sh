#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

FFMPEG_VERSION=8.0
FFMPEG_SHA256=b2751fccb6cc4c77708113cd78b561059b6fa904b24162fa0be2d60273d27b8e
PREFIX="$PWD/third_party/ffmpeg-min"
STAMP="$PREFIX/.stamp"

CONFIGURE_FLAGS=(
    --disable-everything
    --disable-programs --disable-doc --disable-network --disable-autodetect
    --disable-avdevice --disable-avformat --disable-swscale --disable-swresample
    --disable-avfilter
    --enable-decoder=h264
    --enable-hwaccel=h264_vaapi
    --enable-vaapi
    --enable-static --disable-shared --enable-pic --disable-debug
)

WANT="$FFMPEG_VERSION ${CONFIGURE_FLAGS[*]}"
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ] && [ -f "$PREFIX/lib/libavcodec.a" ]; then
    echo "[ok]      ffmpeg-min $FFMPEG_VERSION (third_party/ffmpeg-min)"
    exit 0
fi

command -v nasm >/dev/null 2>&1 || {
    echo "build-ffmpeg.sh: 'nasm' is required to compile FFmpeg x86 assembly." >&2
    echo "                 sudo apt install nasm" >&2
    exit 1
}

echo "[install] ffmpeg-min $FFMPEG_VERSION (h264 decoder + vaapi hwaccel only)..."
BUILD="$PREFIX/src"
rm -rf "$PREFIX"
mkdir -p "$BUILD"

TARBALL="$BUILD/ffmpeg-$FFMPEG_VERSION.tar.xz"
curl -fsSL -o "$TARBALL" "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
echo "$FFMPEG_SHA256  $TARBALL" | sha256sum --check --status - || {
    echo "build-ffmpeg.sh: ffmpeg-$FFMPEG_VERSION.tar.xz failed the checksum check." >&2
    exit 1
}
tar xJ -f "$TARBALL" -C "$BUILD" --strip-components=1
rm -f "$TARBALL"

(
    cd "$BUILD"
    ./configure --prefix="$PREFIX" "${CONFIGURE_FLAGS[@]}" >/dev/null
    make -j"$(nproc)" >/dev/null
    make install >/dev/null
)

rm -rf "$BUILD"
echo "$WANT" >"$STAMP"
echo "[ok]      ffmpeg-min $FFMPEG_VERSION → third_party/ffmpeg-min"
