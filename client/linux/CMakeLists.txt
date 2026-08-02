cmake_minimum_required(VERSION 3.22)

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
# DESKHUB_REQUIRE_LINUX_APP turns the "quietly skip the app" path into a hard
# error. CI sets it so a missing dependency cannot produce a green build that
# shipped no binary.
option(DESKHUB_REQUIRE_LINUX_APP "Fail instead of skipping when the app cannot be built" OFF)
if(DESKHUB_REQUIRE_LINUX_APP)
    set(deskhub_skip_level FATAL_ERROR)
else()
    set(deskhub_skip_level STATUS)
endif()

if(NOT DESKHUB_DEPS_FOUND)
    message(${deskhub_skip_level}
        "Deskhub: skipping the Ubuntu app — development packages are missing. "
        "Run 'make bootstrap' to install them.")
    return()
endif()

set(FFMPEG_MIN_DIR ${CMAKE_SOURCE_DIR}/third_party/ffmpeg-min)
if(NOT EXISTS ${FFMPEG_MIN_DIR}/lib/libavcodec.a)
    message(${deskhub_skip_level}
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

    cpp/capture/ScreenCapture.cpp

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
deskhub_target_warnings(deskhub_app)
