# client/windows/cpp — LỚP NATIVE của app Windows.
#
# Hai target, dựng từ cùng bộ nguồn pipeline:
#   deskhub_win_core (STATIC)  — capture/encode/decode/net/input + hai vòng vai trò
#                                (AgentLoop=host, ClientLoop bản headless nằm ở ClientApi).
#                                Không phải app; là thư viện dùng chung cho DLL.
#   deskhub_native (SHARED)    — DLL phơi C API (DeskhubApi.h) cho frontend C#/WinUI3
#                                ở client/windows/csharp. Đây là ranh giới C#↔C++.
#
# Frontend WinUI3 (client/windows/csharp) do MSBuild dựng, tham chiếu deskhub_native.dll —
# giống client/android: cpp/ (CMake/NDK) tách khỏi java/ (Gradle).
#
# GHI CHÚ M4b: lớp UI Win32 cũ (ui/, main.cpp, client.exe) + đường client dùng cửa sổ
# (ClientLoop/Renderer/InputCapture) đã được GỠ — WinUI3 thay thế hoàn toàn. Vai client
# nay chạy headless qua ClientApi.cpp + PanelRenderer (vẽ vào SwapChainPanel của C#).

# Thư mục chứa nvEncodeAPI.h — pin header NVENC 13.0 (khớp driver hiện tại).
set(NVENC_INTERFACE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/nvenc-13.0/include/ffnvcodec"
    CACHE PATH "Directory containing nvEncodeAPI.h")

# --- Thư viện native dùng chung (không gồm DeskhubApi/AgentApi/ClientApi) ---
add_library(deskhub_win_core STATIC
    AgentLoop.cpp
    ElevatedShare.cpp
    DiagLog.cpp
    ClipboardSync.cpp

    net/UdpSocket.cpp
    net/NetInfo.cpp
    net/Firewall.cpp
    net/HostIdent.cpp
    net/Discovery.cpp
    net/SourceQuery.cpp

    capture/WindowCapture.cpp
    capture/WindowFinder.cpp
    capture/DisplayFinder.cpp
    capture/GpuSelect.cpp
    capture/BmpWriter.cpp

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

# --- DLL C API cho frontend WinUI3 (client/windows/csharp) ---
# Compile ba file C API; mọi thứ khác kéo từ deskhub_win_core theo nhu cầu.
add_library(deskhub_native SHARED
    DeskhubApi.cpp
    AgentApi.cpp
    ClientApi.cpp
)
target_link_libraries(deskhub_native PRIVATE deskhub_win_core)
set_target_properties(deskhub_native PROPERTIES OUTPUT_NAME "deskhub_native")
