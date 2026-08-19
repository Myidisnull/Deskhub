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

if(NOT EXISTS ${FFMPEG_MIN_DIR}/lib/libswscale.a)
    message(${deskhub_skip_level}
        "Deskhub: skipping the Ubuntu app — third_party/ffmpeg-min predates the "
        "swscale-enabled build. Re-run 'scripts/build-ffmpeg.sh'.")
    return()
endif()

add_library(ffmpeg_min INTERFACE)
target_include_directories(ffmpeg_min SYSTEM INTERFACE ${FFMPEG_MIN_DIR}/include)
target_link_libraries(ffmpeg_min INTERFACE
    ${FFMPEG_MIN_DIR}/lib/libavcodec.a
    ${FFMPEG_MIN_DIR}/lib/libswscale.a
    ${FFMPEG_MIN_DIR}/lib/libavutil.a
)

option(DESKHUB_NVENC "Encode on NVIDIA GPUs through NVENC when one is present" ON)
set(NVENC_HEADERS_DIR ${CMAKE_SOURCE_DIR}/third_party/nvenc-13.0/include)
if(DESKHUB_NVENC AND NOT EXISTS ${NVENC_HEADERS_DIR}/ffnvcodec/nvEncodeAPI.h)
    message(${deskhub_skip_level}
        "Deskhub: skipping the Ubuntu app — the third_party/nvenc-13.0 submodule is "
        "not checked out. Run 'git submodule update --init', or pass "
        "-DDESKHUB_NVENC=OFF to build an app that can only encode through VA-API.")
    return()
endif()

pkg_check_modules(DESKHUB_TRAY IMPORTED_TARGET ayatana-appindicator3-0.1)
if(NOT DESKHUB_TRAY_FOUND)
    message(${deskhub_skip_level}
        "Deskhub: the Ubuntu app builds without a tray icon — "
        "libayatana-appindicator3-dev is missing. Run 'make bootstrap'.")
endif()

add_executable(deskhub_app
    gtk/main.cpp
    gtk/MainWindow.cpp
    gtk/PasscodeDialog.cpp
    gtk/TerminalWindow.cpp
    gtk/TrayIcon.cpp
    gtk/ViewerWindow.cpp

    cpp/AgentLoop.cpp

    cpp/capture/ScreenCapture.cpp
    cpp/capture/AudioCapture.cpp

    cpp/encode/VaDisplay.cpp
    cpp/encode/VaEncoder.cpp
    $<$<BOOL:${DESKHUB_NVENC}>:cpp/encode/NvEncoder.cpp>

    cpp/decode/AvDecoder.cpp
    cpp/render/VideoRenderer.cpp

    cpp/input/InputInjector.cpp

)

set_target_properties(deskhub_app PROPERTIES OUTPUT_NAME deskhub)

target_include_directories(deskhub_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp
)

target_link_libraries(deskhub_app PRIVATE core platform PkgConfig::DESKHUB_DEPS ffmpeg_min)
if(DESKHUB_NVENC)
    target_compile_definitions(deskhub_app PRIVATE DESKHUB_HAVE_NVENC)
    target_include_directories(deskhub_app SYSTEM PRIVATE ${NVENC_HEADERS_DIR})
    target_link_libraries(deskhub_app PRIVATE ${CMAKE_DL_LIBS})
endif()
if(DESKHUB_TRAY_FOUND)
    target_compile_definitions(deskhub_app PRIVATE DESKHUB_HAVE_APPINDICATOR)
    target_link_libraries(deskhub_app PRIVATE PkgConfig::DESKHUB_TRAY)
endif()
target_compile_features(deskhub_app PRIVATE cxx_std_20)
deskhub_target_warnings(deskhub_app)

# Uninstalled builds have no hicolor icon, so keep a copy of the window icon
# next to the binary for gtk/main.cpp to fall back on.
add_custom_command(TARGET deskhub_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/icons/deskhub-256.png
        $<TARGET_FILE_DIR:deskhub_app>/deskhub-256.png)
