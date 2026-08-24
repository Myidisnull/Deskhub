set(NVENC_INTERFACE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/nvenc-13.0/include/ffnvcodec"
    CACHE PATH "Directory containing nvEncodeAPI.h")

if(NOT EXISTS "${NVENC_INTERFACE_DIR}/nvEncodeAPI.h")
    message(FATAL_ERROR
        "nvEncodeAPI.h not found in '${NVENC_INTERFACE_DIR}' - "
        "run 'git submodule update --init' (or 'make bootstrap') first, "
        "or point NVENC_INTERFACE_DIR at it")
endif()

add_library(deskhub_win_core STATIC
    AgentLoop.cpp
    ElevatedShare.cpp

    net/Firewall.cpp

    capture/ScreenCapture.cpp
    capture/AudioCapture.cpp
    capture/Downscaler.cpp

    gpu/GpuSelect.cpp
    gpu/D3D11VideoProcessor.cpp

    encode/MfEncoder.cpp
    encode/NvencEncoder.cpp
    encode/EncoderFactory.cpp

    decode/MfDecoder.cpp
    decode/PanelRenderer.cpp
    decode/WinVideoDecoder.cpp

    input/InputInjector.cpp
)

target_compile_definitions(deskhub_win_core PUBLIC UNICODE _UNICODE)
target_include_directories(deskhub_win_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${NVENC_INTERFACE_DIR}"
)
target_link_libraries(deskhub_win_core PUBLIC core platform)
target_link_libraries(deskhub_win_core PUBLIC ws2_32 iphlpapi)

deskhub_target_warnings(deskhub_win_core)
if(MSVC)
    target_compile_options(deskhub_win_core PUBLIC /utf-8)
    target_compile_options(deskhub_win_core PRIVATE /sdl)
endif()
