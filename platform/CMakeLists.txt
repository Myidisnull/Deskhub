cmake_minimum_required(VERSION 3.20)

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
    target_link_libraries(platform PUBLIC ws2_32)
    target_compile_options(platform PRIVATE /W4 /permissive- /sdl)
endif()

if(ANDROID)
    target_link_libraries(platform PUBLIC log)
endif()
