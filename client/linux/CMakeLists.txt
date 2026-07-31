# client/linux — app Ubuntu: MỘT tệp thực thi `deskhub` chứa CẢ HAI VAI
# (host + client), giống bản Windows (một Deskhub.exe) và macOS (một .app).
#
# CẤU TRÚC
#   cpp/  lớp native: capture (portal+PipeWire) · encode (VA-API) · decode
#         (libavcodec) · render (OpenGL) · input (uinput) · net. KHÔNG phụ thuộc
#         GTK — chạy được từ một chương trình khác nếu cần.
#   gtk/  giao diện GTK3. Chỉ lớp này biết tới GTK/GDK.
#
# PHỤ THUỘC HỆ THỐNG (scripts/bootstrap.sh cài, xem docs/17-linux-app.md §1)
#   libpipewire-0.3  bắt hình
#   libva + libva-drm mã hoá H.264 trên GPU
#   gtk+-3.0 + epoxy + egl + libdrm  giao diện và hiển thị
#   Cả bốn đều giữ nguyên SONAME qua các bản Ubuntu nên link động thoải mái.
#
# FFMPEG THÌ NHÚNG TĨNH, KHÔNG LẤY CỦA HỆ THỐNG
#   libavcodec tăng SONAME theo mỗi major FFmpeg (22.04 → .so.58, 24.04 → .so.60,
#   26.04 → .so.62) và apt bản mới KHÔNG có gói cho soname cũ, nên một binary
#   phát hành dựng trên 22.04 sẽ không khởi động nổi trên Ubuntu mới. Ta dựng
#   riêng một FFmpeg tối giản (chỉ decoder h264 + hwaccel vaapi) rồi link tĩnh —
#   xem scripts/build-ffmpeg.sh để biết đầy đủ lý do và bộ cờ.
#
# VÌ SAO KHÔNG CÓ THƯ VIỆN TRUNG GIAN
#   Bản Windows tách client/windows/cpp thành một static lib riêng vì có hai
#   frontend từng dùng chung nó. Ở đây chỉ có một frontend, nên gộp thẳng nguồn
#   vào executable — ít mục tiêu build hơn, link nhanh hơn.
cmake_minimum_required(VERSION 3.20)

# ⚠ DÒ MỀM, KHÔNG REQUIRED — VÀ ĐÓ LÀ CHỦ Ý.
# `make test` / `make coverage` chỉ cần core, và CI chạy chúng trên một runner
# Ubuntu trống trơn. Nếu chỗ này REQUIRED thì bước configure gãy trước cả khi
# core_tests kịp build, chỉ vì máy thiếu libgtk-3-dev — một nền tảng chưa cài
# dependency của app không được phép làm hỏng bài test của lõi.
# Thiếu gói thì bỏ qua app kèm một dòng nhắc; `make bootstrap` cài đủ.
find_package(PkgConfig REQUIRED)
pkg_check_modules(DESKHUB_DEPS IMPORTED_TARGET
    gtk+-3.0
    epoxy
    egl
    libdrm
    libva
    libva-drm
    libpipewire-0.3
)
if(NOT DESKHUB_DEPS_FOUND)
    message(STATUS
        "Deskhub: skipping the Ubuntu app — development packages are missing. "
        "Run 'make bootstrap' to install them (see docs/17-linux-app.md §1).")
    return()
endif()

# FFmpeg tối giản dựng sẵn bởi scripts/build-ffmpeg.sh. Dò MỀM y như trên: máy
# chưa dựng thì bỏ qua app kèm một dòng nhắc, không làm gãy `make test`.
set(FFMPEG_MIN_DIR ${CMAKE_SOURCE_DIR}/third_party/ffmpeg-min)
if(NOT EXISTS ${FFMPEG_MIN_DIR}/lib/libavcodec.a)
    message(STATUS
        "Deskhub: skipping the Ubuntu app — third_party/ffmpeg-min is missing. "
        "Run 'scripts/build-ffmpeg.sh' (or 'make bootstrap').")
    return()
endif()

# Thứ tự BẮT BUỘC: avcodec trước avutil. Linker duyệt .a một lượt từ trái sang
# phải, gặp libavutil.a trước thì các ký hiệu avcodec cần sau đó không còn ai giải.
add_library(ffmpeg_min INTERFACE)
target_include_directories(ffmpeg_min SYSTEM INTERFACE ${FFMPEG_MIN_DIR}/include)
target_link_libraries(ffmpeg_min INTERFACE
    ${FFMPEG_MIN_DIR}/lib/libavcodec.a
    ${FFMPEG_MIN_DIR}/lib/libavutil.a
)

add_executable(deskhub_app
    gtk/main.cpp
    gtk/MainWindow.cpp
    gtk/ShareWindow.cpp
    gtk/ViewerWindow.cpp

    cpp/AgentLoop.cpp
    cpp/ClientLoop.cpp

    cpp/capture/PortalScreenCast.cpp
    cpp/capture/ScreenCapture.cpp
    cpp/capture/SourceEnum.cpp

    cpp/encode/VaDisplay.cpp
    cpp/encode/VaEncoder.cpp

    cpp/decode/AvDecoder.cpp
    cpp/render/VideoRenderer.cpp

    cpp/input/InputInjector.cpp
    cpp/input/LinuxKeyMap.cpp
    cpp/input/LocalInputMonitor.cpp

    cpp/net/NetInfo.cpp
)

# Tên file ra là `deskhub` (chữ thường) theo quy ước tệp thực thi trên Linux; tên
# TARGET vẫn có hậu tố _app cho khớp bản Windows (deskhub_app).
set_target_properties(deskhub_app PROPERTIES OUTPUT_NAME deskhub)

target_include_directories(deskhub_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}      # cho "gtk/..." trong include
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp  # cho "net/...", "capture/..." — khớp bản macOS
)

target_link_libraries(deskhub_app PRIVATE core platform PkgConfig::DESKHUB_DEPS ffmpeg_min)
target_compile_features(deskhub_app PRIVATE cxx_std_20)
target_compile_options(deskhub_app PRIVATE -Wall -Wextra)
