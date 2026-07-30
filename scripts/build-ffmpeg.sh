#!/usr/bin/env bash
# =============================================================================
# build-ffmpeg.sh — dựng một FFmpeg TỐI GIẢN, link tĩnh, chỉ để giải mã H.264.
#
# VÌ SAO KHÔNG DÙNG libavcodec CỦA HỆ THỐNG
#   App Ubuntu phát hành dưới dạng MỘT tệp thực thi dựng trên 22.04, và cách đó
#   hoạt động với mọi thư viện có SONAME ổn định (libgtk-3.so.0, libva.so.2,
#   libpipewire-0.3.so.0 giữ nguyên qua các bản Ubuntu). libavcodec thì KHÔNG:
#   nó tăng SONAME theo mỗi bản major của FFmpeg —
#       Ubuntu 22.04 → libavcodec.so.58   24.04 → .so.60   26.04 → .so.62
#   nên bản dựng trên 22.04 đòi .so.58, mà 24.04+ không những thiếu nó, apt còn
#   KHÔNG có gói nào cài được (`apt-cache policy libavcodec58` → Candidate: none).
#   Tức là binary phát hành không khởi động nổi trên Ubuntu mới.
#
#   `dlopen` nhiều SONAME nghe có vẻ giải quyết được, nhưng KHÔNG an toàn: code
#   đọc thẳng field của AVCodecContext/AVFrame (ctx->get_format, f->data[3]...),
#   mà offset của chúng do header lúc BIÊN DỊCH quyết còn struct thật do thư viện
#   lúc CHẠY cấp phát. Hai major FFmpeg là hai layout khác nhau — bằng chứng nằm
#   ngay trong header: `unsigned properties` của AVCodecContext bị bọc trong
#   `#if FF_API_CODEC_PROPS` (= major < 63), nó nằm GIỮA struct nên mọi field sau
#   nó xê dịch. Sai kiểu này không crash ngay, nó hỏng ngầm.
#
#   Nên: tự dựng FFmpeg, nhúng tĩnh. Biên dịch và chạy cùng một phiên bản, hết
#   chuyện SONAME, và bản phát hành chạy trên mọi bản Ubuntu.
#
# VÌ SAO NHỎ
#   `--disable-everything` tắt sạch rồi bật lại đúng hai thứ: decoder h264 và
#   hwaccel h264_vaapi. Không encoder, không muxer/demuxer, không filter, không
#   network. `--disable-autodetect` để configure KHÔNG tự bắt libx264/libvpx...
#   của máy build — thiếu nó thì binary lại phụ thuộc động vào cả rừng codec.
#   Kết quả: hai .a khoảng 3.5 MB, và binary cuối chỉ phình đúng phần linker giữ.
#
# GIẤY PHÉP
#   Bộ cờ này cho ra "LGPL version 2.1 or later" (configure tự in ra — không bật
#   --enable-gpl, không libx264). Deskhub là mã nguồn mở, có sẵn cả script này,
#   nên yêu cầu cho phép relink của LGPL coi như đã thoả.
#
# VÌ SAO GHIM 8.0 CHỨ KHÔNG PHẢI 7.1
#   7.1.1 KHÔNG build được ở cấu hình chỉ-h264: `CONFIG_H264_SEI` kéo vào
#   h2645_sei.o (có gọi ff_aom_uninit_film_grain_params) nhưng aom_film_grain.o
#   chỉ được biên dịch khi bật HEVC → link gãy vì thiếu ký hiệu. 8.0 sửa rồi:
#   aom_film_grain.o nằm luôn trong CONFIG_H264_SEI (libavcodec/Makefile).
#
# Chạy lại nhiều lần thì rẻ: có stamp khớp version + cờ là thoát ngay.
#
# LIÊN QUAN: client/linux/CMakeLists.txt (nơi link), docs/17-linux-app.md §1
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

FFMPEG_VERSION=8.0
PREFIX="$PWD/third_party/ffmpeg-min"
STAMP="$PREFIX/.stamp"

# Bật x86asm: đường giải mã chính là VA-API trên GPU, nhưng vẫn có nhánh lùi về
# CPU khi máy không có driver VA-API — bỏ SIMD thì nhánh đó chậm tới mức vô dụng.
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

# Stamp gồm cả cờ: đổi cờ mà quên xoá thư mục thì vẫn build lại, không dùng nhầm
# bản cũ.
WANT="$FFMPEG_VERSION ${CONFIGURE_FLAGS[*]}"
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT" ] && [ -f "$PREFIX/lib/libavcodec.a" ]; then
    echo "[ok]      ffmpeg-min $FFMPEG_VERSION (third_party/ffmpeg-min)"
    exit 0
fi

command -v nasm >/dev/null 2>&1 || {
    echo "build-ffmpeg.sh: cần 'nasm' để biên dịch phần assembly x86 của FFmpeg." >&2
    echo "                 sudo apt install nasm" >&2
    exit 1
}

echo "[install] ffmpeg-min $FFMPEG_VERSION (chỉ decoder h264 + hwaccel vaapi)..."
BUILD="$PREFIX/src"
rm -rf "$PREFIX"
mkdir -p "$BUILD"

curl -sSL "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz" \
    | tar xJ -C "$BUILD" --strip-components=1

(
    cd "$BUILD"
    ./configure --prefix="$PREFIX" "${CONFIGURE_FLAGS[@]}" >/dev/null
    make -j"$(nproc)" >/dev/null
    make install >/dev/null
)

# Nguồn chỉ cần lúc build; giữ lại chỉ tổ phình cây thư mục (~200 MB).
rm -rf "$BUILD"
echo "$WANT" >"$STAMP"
echo "[ok]      ffmpeg-min $FFMPEG_VERSION → third_party/ffmpeg-min"
