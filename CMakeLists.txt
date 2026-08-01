cmake_minimum_required(VERSION 3.25)
project(Deskhub LANGUAGES CXX)

# Objective-C++ sources in platform/: the local-input monitor (macOS) and the
# VideoToolbox decoder (macOS + iOS).
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_subdirectory(core)
add_subdirectory(platform)

if(CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(deskhub_integration_tests_default OFF)
else()
    set(deskhub_integration_tests_default ON)
endif()
option(DESKHUB_INTEGRATION_TESTS "Build integration_tests"
    ${deskhub_integration_tests_default})
if(DESKHUB_INTEGRATION_TESTS)
    add_subdirectory(tests/integration)
endif()

if(WIN32)
    add_subdirectory(client/windows/cpp)
    add_subdirectory(client/windows/win32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(DESKHUB_LINUX_APP "Build the Ubuntu desktop app" ON)
    if(DESKHUB_LINUX_APP)
        add_subdirectory(client/linux)
    endif()
endif()
