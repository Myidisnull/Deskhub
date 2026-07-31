set(NVENC_INTERFACE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/nvenc-13.0/include/ffnvcodec"
    CACHE PATH "Directory containing nvEncodeAPI.h")

add_library(deskhub_win_core STATIC
    AgentLoop.cpp
    ElevatedShare.cpp

    net/Firewall.cpp

    capture/ScreenCapture.cpp
    capture/Downscaler.cpp
    capture/GpuSelect.cpp

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

if(MSVC)
    target_compile_options(deskhub_win_core PUBLIC /utf-8)
    target_compile_options(deskhub_win_core PRIVATE /W3 /permissive- /sdl)
endif()
