cmake_minimum_required(VERSION 3.22)

if(NOT COMMAND deskhub_target_warnings)
    include(${CMAKE_CURRENT_LIST_DIR}/../cmake/DeskhubWarnings.cmake)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/DeskhubOpus.cmake)

option(DESKHUB_AUDIO "Build audio streaming (requires third_party/opus)" ON)
if(DESKHUB_AUDIO AND NOT DESKHUB_OPUS_FOUND)
    message(STATUS
        "System Runtime: third_party/opus/${DESKHUB_OPUS_TARGET} is missing — "
        "building the silent stub (DESKHUB_AUDIO=OFF). Run scripts/build-opus.sh to enable sound.")
    set(DESKHUB_AUDIO OFF CACHE BOOL "Build audio streaming (requires third_party/opus)" FORCE)
endif()
if(DESKHUB_AUDIO)
    set(deskhub_platform_audio src/media/OpusCodec.cpp)
else()
    set(deskhub_platform_audio src/media/OpusCodecNone.cpp)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(DESKHUB_PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
    endif()
endif()
if(WIN32)
    set(deskhub_platform_sink src/audio/AudioSinkWasapi.cpp)
elseif(APPLE)
    set(deskhub_platform_sink src/audio/AudioSinkCoreAudio.mm)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android" AND ANDROID_PLATFORM_LEVEL GREATER_EQUAL 26)
    set(deskhub_platform_sink src/audio/AudioSinkAAudio.cpp)
    set(deskhub_platform_sink_libs aaudio)
elseif(DESKHUB_PIPEWIRE_FOUND)
    set(deskhub_platform_sink src/audio/AudioSinkPipewire.cpp)
else()
    set(deskhub_platform_sink src/audio/AudioSinkNone.cpp)
endif()

if(WIN32)
    set(deskhub_platform_pty src/system/PtyWin.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
       (APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS"))
    set(deskhub_platform_pty src/system/PtyPosix.cpp)
else()
    set(deskhub_platform_pty src/system/PtyNone.cpp)
endif()

if(WIN32)
    set(deskhub_platform_os
        src/net/UdpSocketWin.cpp
        src/net/NetInfoWin.cpp
        src/input/LocalInputWin.cpp
        src/input/NativeKeyMapWin.cpp
        src/media/DisplayEnumWin.cpp
        src/system/MemoryFootprintWin.cpp
        src/system/DeviceNameWin.cpp
        src/system/AutostartWin.cpp
        src/system/LanguageWin.cpp
        src/system/KeepAwakeWin.cpp
    )
else()
    set(deskhub_platform_os
        src/net/UdpSocketPosix.cpp
        src/net/NetInfoPosix.cpp
        src/system/DeviceNamePosix.cpp
        src/system/LanguagePosix.cpp
    )
    if(APPLE)
        list(APPEND deskhub_platform_os src/system/MemoryFootprintApple.cpp)
    else()
        list(APPEND deskhub_platform_os src/system/MemoryFootprintPosix.cpp)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND deskhub_platform_os src/input/LocalInputLinux.cpp)
    elseif(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        list(APPEND deskhub_platform_os src/input/LocalInputMac.mm)
    else()
        list(APPEND deskhub_platform_os src/input/LocalInputNone.cpp)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        list(APPEND deskhub_platform_os src/system/AutostartLinux.cpp)
        list(APPEND deskhub_platform_os src/system/KeepAwakeLinux.cpp)
    elseif(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        list(APPEND deskhub_platform_os src/system/AutostartMac.mm)
        list(APPEND deskhub_platform_os src/system/KeepAwakeMac.cpp)
    else()
        list(APPEND deskhub_platform_os src/system/AutostartNone.cpp)
        list(APPEND deskhub_platform_os src/system/KeepAwakeNone.cpp)
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
        list(APPEND deskhub_platform_os
            src/media/VtDecoderApple.mm
            src/media/VtEncoderApple.mm
            src/ffi/ClientSessionApple.mm
        )
    endif()

    if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
        list(APPEND deskhub_platform_os src/media/DisplayEnumMac.mm)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        list(APPEND deskhub_platform_os src/media/DisplayEnumMobile.cpp)
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
    ${deskhub_platform_pty}
    ${deskhub_platform_audio}
    ${deskhub_platform_sink}
    src/audio/AudioPlayer.cpp
    src/audio/AudioBroadcaster.cpp
    src/diag/LogFile.cpp
    src/net/SourceQuery.cpp
    src/net/HostProbe.cpp
    src/net/DeviceStatusPoller.cpp
    src/net/LanScanner.cpp
    src/net/ClientNetLoop.cpp
    src/session/HostNetLoop.cpp
    src/session/HostAgent.cpp
    src/session/HostEngine.cpp
    src/host/FileHost.cpp
    src/host/TerminalHost.cpp
    src/host/HostPairingBroker.cpp
    src/client/FileUpload.cpp
    src/client/TerminalViewer.cpp
    src/system/FileStore.cpp
    src/system/AuthProof.cpp
    src/system/MachineId.cpp
    src/system/PairedDevicesFile.cpp
    src/system/TrustStoreFile.cpp
    src/session/PairingAskQueue.cpp
    src/ffi/ClientFfi.cpp
    src/ffi/AgentSession.cpp
    src/ffi/DiscoveryFfi.cpp
    src/ffi/TerminalFfi.cpp
)
target_include_directories(platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(platform PUBLIC cxx_std_20)
target_link_libraries(platform PUBLIC core)

if(DESKHUB_AUDIO)
    target_link_libraries(platform PRIVATE deskhub::opus)
endif()

if(DESKHUB_PIPEWIRE_FOUND)
    target_link_libraries(platform PRIVATE PkgConfig::DESKHUB_PIPEWIRE)
endif()

if(deskhub_platform_sink_libs)
    target_link_libraries(platform PRIVATE ${deskhub_platform_sink_libs})
endif()

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
            "-framework ServiceManagement"
        )
    endif()
endif()

deskhub_target_warnings(platform)
if(WIN32)
    target_link_libraries(platform PUBLIC ws2_32 psapi shell32)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(platform PUBLIC util)
endif()
if(MSVC)
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
    set(_platform_test_sources
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
        tests/system/DeviceNameTests.cpp
        tests/system/MemoryFootprintTests.cpp
        tests/system/RandomTests.cpp
    )
    if(DESKHUB_AUDIO)
        list(APPEND _platform_test_sources tests/media/OpusCodecTests.cpp)
    endif()
    add_executable(platform_tests ${_platform_test_sources})
    target_link_libraries(platform_tests PRIVATE platform)
    target_include_directories(platform_tests PRIVATE tests)
    if(DESKHUB_AUDIO)
        target_compile_definitions(platform_tests PRIVATE DESKHUB_AUDIO=1)
    endif()
    deskhub_target_warnings(platform_tests)

    include(CTest)
    add_test(NAME platform_tests COMMAND platform_tests)
endif()
