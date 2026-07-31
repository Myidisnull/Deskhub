cmake_minimum_required(VERSION 3.20)

if(WIN32)
    set(deskhub_platform_os
        src/UdpSocketWin.cpp
        src/NetInfoWin.cpp
        src/LocalInputWin.cpp
    )
else()
    set(deskhub_platform_os
        src/UdpSocketPosix.cpp
        src/NetInfoPosix.cpp
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND deskhub_platform_os src/LocalInputLinux.cpp)
    elseif(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        list(APPEND deskhub_platform_os src/LocalInputMac.mm)
    else()
        list(APPEND deskhub_platform_os src/LocalInputNone.cpp)
    endif()

    if(APPLE)
        list(APPEND deskhub_platform_os src/VtDecoderApple.mm)
    endif()
endif()

add_library(platform STATIC
    ${deskhub_platform_os}
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
