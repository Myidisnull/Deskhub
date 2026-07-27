# client/windows/cpp — LỚP NATIVE của app Windows.
#
# Một target thư viện duy nhất:
#   deskhub_win_core (STATIC)  — capture/encode/decode/net/input + vòng vai host
#                                (AgentLoop); vai client chạy headless qua ClientApi.cpp
#                                (app win32 compile thẳng file đó vào exe).
#
# App Windows duy nhất là client/windows/win32 (Win32 thuần, một Deskhub.exe).

# Thư mục chứa nvEncodeAPI.h — pin header NVENC 13.0 (khớp driver hiện tại).
set(NVENC_INTERFACE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/nvenc-13.0/include/ffnvcodec"
    CACHE PATH "Directory containing nvEncodeAPI.h")

# --- Thư viện native dùng chung (không gồm DeskhubApi/AgentApi/ClientApi) ---
add_library(deskhub_win_core STATIC
    AgentLoop.cpp
    ElevatedShare.cpp
    DiagLog.cpp

    net/UdpSocket.cpp
    net/NetInfo.cpp
    net/Firewall.cpp
    net/SourceQuery.cpp

    capture/ScreenCapture.cpp
    capture/DisplayFinder.cpp
    capture/GpuSelect.cpp

    encode/MfEncoder.cpp
    encode/NvencEncoder.cpp
    encode/EncoderFactory.cpp

    decode/MfDecoder.cpp
    decode/PanelRenderer.cpp

    input/InputInjector.cpp
    input/LocalInputMonitor.cpp
)

# PUBLIC: mọi target link vào lib này thừa hưởng UNICODE + include dir, để #include
# ghi đủ đường dẫn nhóm ("net/UdpSocket.h") ở các file C API.
target_compile_definitions(deskhub_win_core PUBLIC UNICODE _UNICODE)
target_include_directories(deskhub_win_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${NVENC_INTERFACE_DIR}"
)
target_link_libraries(deskhub_win_core PUBLIC core platform)
# Lib hệ thống dùng qua ranh giới lib: NetInfo cần ws2_32 (inet_ntop) + iphlpapi
# (GetAdaptersAddresses). Trước đây kéo ngầm qua #pragma comment(lib) trong UdpSocket,
# nhưng DLL enumeration không nạp object đó nên phải khai báo tường minh ở đây.
target_link_libraries(deskhub_win_core PUBLIC ws2_32 iphlpapi)

if(MSVC)
    # /utf-8: nguồn là UTF-8 (comment tiếng Việt + chuỗi L"..." ngoài ASCII).
    target_compile_options(deskhub_win_core PUBLIC /utf-8)
    target_compile_options(deskhub_win_core PRIVATE /W3 /permissive- /sdl)
endif()
