cmake_minimum_required(VERSION 3.20)

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
        "Run 'make bootstrap' to install them.")
    return()
endif()

set(FFMPEG_MIN_DIR ${CMAKE_SOURCE_DIR}/third_party/ffmpeg-min)
if(NOT EXISTS ${FFMPEG_MIN_DIR}/lib/libavcodec.a)
    message(STATUS
        "Deskhub: skipping the Ubuntu app — third_party/ffmpeg-min is missing. "
        "Run 'scripts/build-ffmpeg.sh' (or 'make bootstrap').")
    return()
endif()

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

    cpp/capture/PortalScreenCast.cpp
    cpp/capture/ScreenCapture.cpp
    cpp/capture/DisplayEnumLinux.cpp

    cpp/encode/VaDisplay.cpp
    cpp/encode/VaEncoder.cpp

    cpp/decode/AvDecoder.cpp
    cpp/render/VideoRenderer.cpp

    cpp/input/InputInjector.cpp
    cpp/input/LinuxKeyMap.cpp

)

set_target_properties(deskhub_app PROPERTIES OUTPUT_NAME deskhub)

target_include_directories(deskhub_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp
)

target_link_libraries(deskhub_app PRIVATE core platform PkgConfig::DESKHUB_DEPS ffmpeg_min)
target_compile_features(deskhub_app PRIVATE cxx_std_20)
target_compile_options(deskhub_app PRIVATE -Wall -Wextra)
