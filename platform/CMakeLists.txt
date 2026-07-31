# platform — lớp MỎNG bọc những thứ core/ không được phép chạm (header hệ điều
# hành). Dùng chung giữa các client OS; API giữ nguyên tên giữa mọi nền tảng để
# code port qua lại đọc y hệt.
#
# Từ 31/07/2026 đây là STATIC chứ không còn INTERFACE: UdpSocket và SourceQuery đã
# được gộp về đây từ năm bản chép tay trong client/*/net/ (lý do đầy đủ ở đầu
# deskhubp/UdpSocket.h). Header-only vẫn còn: Clock.h, Random.h, LogFile.h, Log.h.
#
# PHỤ THUỘC: platform -> core (SourceQuery dùng deskhub/protocol/Wire.h). MỘT CHIỀU
# và phải giữ nguyên như vậy — core tuyệt đối không được biết tới platform, đó là
# điều giữ cho core build được bằng mọi toolchain và test được offline.
cmake_minimum_required(VERSION 3.20)

# Socket: bốn nền POSIX dùng chung một .cpp, Windows một .cpp riêng. Đây là toàn bộ
# chỗ #ifdef hệ điều hành của tầng này — chọn ở CMake thay vì rải trong mã nguồn.
if(WIN32)
    set(deskhub_platform_socket src/UdpSocketWin.cpp)
else()
    set(deskhub_platform_socket src/UdpSocketPosix.cpp)
endif()

add_library(platform STATIC
    ${deskhub_platform_socket}
    src/SourceQuery.cpp
)
target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(platform PUBLIC cxx_std_20)
target_link_libraries(platform PUBLIC core)

if(WIN32)
    # ws2_32 khai báo ở đây thay vì #pragma comment(lib) trong .cpp: pragma đó chỉ
    # MSVC hiểu, còn CMake thì mọi trình liên kết trên Windows đều nghe.
    target_link_libraries(platform PUBLIC ws2_32)
    target_compile_options(platform PRIVATE /W4 /permissive- /sdl)
endif()

if(ANDROID)
    # LOGI/LOGW/LOGE của deskhubp/Log.h đi qua __android_log_print.
    target_link_libraries(platform PUBLIC log)
endif()
