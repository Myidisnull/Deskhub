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
#   libavcodec/libavutil giải mã H.264 (hwaccel VA-API)
#   gtk+-3.0 + epoxy + egl + libdrm  giao diện và hiển thị
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
    libavcodec
    libavutil
)
if(NOT DESKHUB_DEPS_FOUND)
    message(STATUS
        "Deskhub: skipping the Ubuntu app — development packages are missing. "
        "Run 'make bootstrap' to install them (see docs/17-linux-app.md §1).")
    return()
endif()

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
    cpp/net/SourceQuery.cpp
    cpp/net/UdpSocket.cpp
)

# Tên file ra là `deskhub` (chữ thường) theo quy ước tệp thực thi trên Linux; tên
# TARGET vẫn có hậu tố _app cho khớp bản Windows (deskhub_app).
set_target_properties(deskhub_app PROPERTIES OUTPUT_NAME deskhub)

target_include_directories(deskhub_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}      # cho "gtk/..." trong include
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp  # cho "net/...", "capture/..." — khớp bản macOS
)

target_link_libraries(deskhub_app PRIVATE core platform PkgConfig::DESKHUB_DEPS)
target_compile_features(deskhub_app PRIVATE cxx_std_20)
target_compile_options(deskhub_app PRIVATE -Wall -Wextra)
