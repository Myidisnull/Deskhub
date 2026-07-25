# client/windows/cpp — LỚP NATIVE của app Windows.
#
# Ba target, dựng từ cùng một bộ nguồn pipeline:
#   deskhub_win_core (STATIC)  — toàn bộ capture/encode/decode/net/input + hai vòng
#                                vai trò (AgentLoop=host, ClientLoop=client). Không
#                                phải app; chỉ là thư viện dùng chung cho hai target dưới.
#   client (EXE, WIN32)        — app Win32 cũ (main.cpp + ui/). GIỮ LẠI trong lúc
#                                chuyển sang WinUI3 để build luôn xanh và còn đường lui.
#   deskhub_native (SHARED)    — DLL phơi C API (DeskhubApi.h) cho frontend C#/WinUI3
#                                ở client/windows/csharp. Đây là ranh giới C#↔C++.
#
# Frontend WinUI3 (client/windows/csharp) do MSBuild dựng, tham chiếu deskhub_native.dll —
# giống client/android: cpp/ (CMake/NDK) tách khỏi java/ (Gradle).

# Thư mục chứa nvEncodeAPI.h — pin header NVENC 13.0 (khớp driver hiện tại). Đường dẫn
# lùi ba cấp vì file này giờ nằm ở client/windows/cpp/ (trước ở client/windows/).
set(NVENC_INTERFACE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/nvenc-13.0/include/ffnvcodec"
    CACHE PATH "Directory containing nvEncodeAPI.h")

# RC = trình biên dịch tài nguyên Windows (client.rc: manifest + version info).
enable_language(RC)

# --- Thư viện native dùng chung (không gồm main.cpp/client.rc/DeskhubApi.cpp) ---
# Nhóm theo thư mục con: net/ capture/ encode/ decode/ input/ ui/ + gốc (hai vòng vai
# trò + tiện ích). ui/ vẫn nằm đây để client.exe cũ còn build; frontend WinUI3 KHÔNG
# đụng tới nó (DLL chỉ kéo các đối tượng nó thật sự tham chiếu).
add_library(deskhub_win_core STATIC
    AgentLoop.cpp
    ClientLoop.cpp
    ElevatedShare.cpp
    DiagLog.cpp
    ClipboardSync.cpp

    net/UdpSocket.cpp
    net/NetInfo.cpp
    net/Firewall.cpp

    capture/WindowCapture.cpp
    capture/WindowFinder.cpp
    capture/GpuSelect.cpp
    capture/BmpWriter.cpp

    encode/MfEncoder.cpp
    encode/NvencEncoder.cpp
    encode/EncoderFactory.cpp

    decode/MfDecoder.cpp
    decode/Renderer.cpp
    decode/PanelRenderer.cpp

    input/InputCapture.cpp
    input/InputInjector.cpp
    input/LocalInputMonitor.cpp

    ui/MainMenuWindow.cpp
    ui/WindowPickerDialog.cpp
    ui/SourcePickerDialog.cpp
    ui/SessionWindow.cpp
    ui/ViewerWindow.cpp
)

# PUBLIC: mọi target link vào lib này thừa hưởng UNICODE + include dir, để #include
# ghi đủ đường dẫn nhóm ("net/UdpSocket.h") ở cả DeskhubApi.cpp lẫn main.cpp.
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
    # /utf-8: nguồn là UTF-8 (comment tiếng Việt + chuỗi L"..." ngoài ASCII). Thiếu cờ
    # này MSVC đọc theo codepage hệ thống -> chuỗi rộng hỏng.
    target_compile_options(deskhub_win_core PUBLIC /utf-8)
    target_compile_options(deskhub_win_core PRIVATE /W3 /permissive- /sdl)
endif()

# --- App Win32 cũ (chuyển tiếp): main.cpp + tài nguyên, link lib ở trên ---
# WIN32: GUI thuần, điểm vào wWinMain, không bung console. client.rc nhúng manifest +
# version info (giảm báo nhầm virus).
add_executable(client WIN32
    main.cpp
    client.rc
)
target_link_libraries(client PRIVATE deskhub_win_core)
if(MSVC)
    # Manifest nhúng qua client.rc — tắt manifest tự sinh của linker (tránh LNK4078).
    target_link_options(client PRIVATE /MANIFEST:NO)
endif()

# --- DLL C API cho frontend WinUI3 (client/windows/csharp) ---
# Chỉ compile DeskhubApi.cpp; mọi thứ khác kéo từ deskhub_win_core theo nhu cầu
# (linker MSVC chỉ nạp .obj được tham chiếu — DLL enumeration của M1 không kéo ui/).
add_library(deskhub_native SHARED
    DeskhubApi.cpp
    AgentApi.cpp
    ClientApi.cpp
)
target_link_libraries(deskhub_native PRIVATE deskhub_win_core)
# Đặt tên file DLL cố định là deskhub_native.dll để phía C# DllImport khớp.
set_target_properties(deskhub_native PROPERTIES OUTPUT_NAME "deskhub_native")
