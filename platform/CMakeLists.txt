cmake_minimum_required(VERSION 3.22)

if(NOT COMMAND deskhub_target_warnings)
    include(${CMAKE_CURRENT_LIST_DIR}/../cmake/DeskhubWarnings.cmake)
endif()

if(WIN32)
    set(deskhub_platform_os
        src/net/UdpSocketWin.cpp
        src/net/NetInfoWin.cpp
        src/input/LocalInputWin.cpp
        src/input/NativeKeyMapWin.cpp
        src/media/DisplayEnumWin.cpp
    )
else()
    set(deskhub_platform_os
        src/net/UdpSocketPosix.cpp
        src/net/NetInfoPosix.cpp
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND deskhub_platform_os src/input/LocalInputLinux.cpp)
    elseif(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        list(APPEND deskhub_platform_os src/input/LocalInputMac.mm)
    else()
        list(APPEND deskhub_platform_os src/input/LocalInputNone.cpp)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND deskhub_platform_os src/input/NativeKeyMapLinux.cpp)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        list(APPEND deskhub_platform_os src/input/NativeKeyMapIos.cpp)
    elseif(APPLE)
        list(APPEND deskhub_platform_os src/input/NativeKeyMapMac.cpp)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
        list(APPEND deskhub_platform_os src/input/NativeKeyMapAndroid.cpp)
    else()
        list(APPEND deskhub_platform_os src/input/NativeKeyMapNone.cpp)
    endif()

    if(APPLE)
        list(APPEND deskhub_platform_os src/media/VtDecoderApple.mm src/ffi/ClientSessionApple.mm)
    endif()

    if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        list(APPEND deskhub_platform_os src/media/DisplayEnumMac.mm)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(PkgConfig)
        if(PkgConfig_FOUND)
            pkg_check_modules(DESKHUB_PORTAL IMPORTED_TARGET gio-2.0 gio-unix-2.0)
        endif()
        if(DESKHUB_PORTAL_FOUND)
            list(APPEND deskhub_platform_os
                src/media/PortalScreenCast.cpp
                src/media/DisplayEnumLinux.cpp
            )
        else()
            message(STATUS
                "Deskhub: platform falls back to a stub display enumerator — "
                "gio-2.0 development packages are missing. Run 'make bootstrap'.")
            list(APPEND deskhub_platform_os src/media/DisplayEnumNone.cpp)
        endif()
    else()
        list(APPEND deskhub_platform_os src/media/DisplayEnumNone.cpp)
    endif()
endif()

add_library(platform STATIC
    ${deskhub_platform_os}
    src/net/SourceQuery.cpp
    src/net/HostProbe.cpp
    src/net/DeviceStatusPoller.cpp
    src/net/LanScanner.cpp
    src/net/ClientNetLoop.cpp
    src/session/HostNetLoop.cpp
    src/session/HostAgent.cpp
    src/session/HostEngine.cpp
    src/ffi/ClientFfi.cpp
    src/ffi/AgentSession.cpp
    src/ffi/DiscoveryFfi.cpp
)
target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(platform PUBLIC cxx_std_20)
target_link_libraries(platform PUBLIC core)

# The Objective-C++ sources are written for ARC (__bridge_retained / __bridge_transfer),
# and the Xcode app targets already build their own .mm files with CLANG_ENABLE_OBJC_ARC.
# Without this, those casts silently do nothing here and objects come back dangling.
if(APPLE)
    target_compile_options(platform PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)

    target_link_libraries(platform PUBLIC
        "-framework Foundation"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreGraphics"
        "-framework VideoToolbox"
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_link_libraries(platform PUBLIC "-framework UIKit")
    else()
        target_link_libraries(platform PUBLIC
            "-framework AppKit"
            "-framework ScreenCaptureKit"
        )
    endif()
endif()

deskhub_target_warnings(platform)
if(WIN32)
    target_link_libraries(platform PUBLIC ws2_32)
    target_compile_options(platform PRIVATE /sdl)
endif()

if(DESKHUB_PORTAL_FOUND)
    target_link_libraries(platform PRIVATE PkgConfig::DESKHUB_PORTAL)
endif()

if(ANDROID)
    target_link_libraries(platform PUBLIC log)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(deskhub_platform_tests_default OFF)
else()
    set(deskhub_platform_tests_default ON)
endif()
option(DESKHUB_PLATFORM_TESTS "Build platform_tests" ${deskhub_platform_tests_default})

if(DESKHUB_PLATFORM_TESTS)
    add_executable(platform_tests
        tests/TestMain.cpp
        tests/support/TestSupport.cpp

        tests/diag/LogFileTests.cpp
        tests/ffi/FfiTextTests.cpp
        tests/input/LocalInputGateTests.cpp
        tests/net/NetAddrTests.cpp
        tests/net/SourceQueryTests.cpp
        tests/net/HostProbeTests.cpp
        tests/net/LanScannerTests.cpp
        tests/net/UdpSocketTests.cpp
        tests/system/AppDataFileTests.cpp
        tests/session/HostCallbackTests.cpp
        tests/system/ClockTests.cpp
        tests/system/RandomTests.cpp
    )
    target_link_libraries(platform_tests PRIVATE platform)
    target_include_directories(platform_tests PRIVATE tests)
    deskhub_target_warnings(platform_tests)

    include(CTest)
    add_test(NAME platform_tests COMMAND platform_tests)
endif()
