# Remote game streaming/control — build bằng CMake + Ninja.
# Cấu trúc: core/ (thuần, dùng chung mọi OS) + client/<os>/ (app từng nền tảng,
# mỗi OS một file duy nhất — chứa cả vai trò host lẫn client).
# Windows: mở thư mục này bằng Visual Studio (Open Folder) hoặc từ Developer prompt:
#   cmake --preset x64-debug && cmake --build --preset x64-debug
cmake_minimum_required(VERSION 3.25)
project(Deskhub LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Bật testing Ở GỐC trước khi vào core/: nếu không, add_test của core (được đăng ký
# trong subdir) không được CTestTestfile.cmake ở gốc trỏ tới, và `ctest --test-dir
# out/build/x64-debug` báo "No tests were found". Có dòng này thì ctest chạy từ thư
# mục build gốc thấy được core_tests.
enable_testing()

add_subdirectory(core)
add_subdirectory(platform)
if(WIN32)
    # client/windows/cpp = lớp native (capture/encode/decode/net + C API DeskhubApi.h).
    # client/windows/win32 = app Windows DUY NHẤT (Win32 thuần, một Deskhub.exe).
    add_subdirectory(client/windows/cpp)
    add_subdirectory(client/windows/win32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # client/linux = app Ubuntu DUY NHẤT (GTK3 + native, một tệp `deskhub`), cả
    # hai vai. Nó tự bỏ qua chính mình nếu máy thiếu gói -dev (PipeWire, VA-API,
    # FFmpeg, GTK3) — lý do ở client/linux/CMakeLists.txt. Tắt hẳn bằng:
    #   cmake --preset x64-debug -DDESKHUB_LINUX_APP=OFF
    option(DESKHUB_LINUX_APP "Build the Ubuntu desktop app" ON)
    if(DESKHUB_LINUX_APP)
        add_subdirectory(client/linux)
    endif()
endif()
# client/android KHÔNG nằm ở đây: nó do Gradle/NDK dựng (client/android/, xem
# docs/08-android-client.md), và CMakeLists của nó tự add_subdirectory(core).
# client/macos và client/ios do Xcode dựng (xem make/macos.mk, make/ios.mk).
