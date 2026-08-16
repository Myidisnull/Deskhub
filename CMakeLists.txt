cmake_minimum_required(VERSION 3.25)
project(Deskhub LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(DeskhubWarnings)

include(DeskhubQuiche)

option(DESKHUB_MSVC_STATIC_RUNTIME "Link the MSVC runtime statically" ON)
if(MSVC AND DESKHUB_QUICHE_FOUND)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
    add_compile_definitions(_ITERATOR_DEBUG_LEVEL=0 _HAS_ITERATOR_DEBUGGING=0)
    add_compile_options("$<$<CONFIG:Debug>:/U_DEBUG>")
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
elseif(MSVC AND DESKHUB_MSVC_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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

option(DESKHUB_LINUX_APP "Build the Ubuntu desktop app" ON)

if(WIN32)
    add_subdirectory(client/windows/cpp)
    add_subdirectory(client/windows/win32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(DESKHUB_LINUX_APP)
        add_subdirectory(client/linux)
    endif()
endif()
