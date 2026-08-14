# Locates the quiche static library that scripts/build-quiche.sh produced and
# exposes it as the imported target deskhub::quiche. Sets DESKHUB_QUICHE_FOUND.
#
# scripts/build-quiche.sh writes one directory per rust target under
# third_party/quiche/, plus a shared include/ holding quiche.h and the BoringSSL
# headers that come with it.

set(DESKHUB_QUICHE_ROOT ${CMAKE_CURRENT_LIST_DIR}/../third_party/quiche)

function(deskhub_quiche_rust_target out_var)
    if(WIN32)
        set(${out_var} x86_64-pc-windows-msvc PARENT_SCOPE)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
        if(ANDROID_ABI STREQUAL "arm64-v8a")
            set(${out_var} aarch64-linux-android PARENT_SCOPE)
        elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
            set(${out_var} armv7-linux-androideabi PARENT_SCOPE)
        else()
            set(${out_var} x86_64-linux-android PARENT_SCOPE)
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if(CMAKE_OSX_SYSROOT MATCHES "Simulator")
            set(${out_var} aarch64-apple-ios-sim PARENT_SCOPE)
        else()
            set(${out_var} aarch64-apple-ios PARENT_SCOPE)
        endif()
    elseif(APPLE)
        if(EXISTS ${DESKHUB_QUICHE_ROOT}/macos-universal/libquiche.a)
            set(${out_var} macos-universal PARENT_SCOPE)
        elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
            set(${out_var} x86_64-apple-darwin PARENT_SCOPE)
        else()
            set(${out_var} aarch64-apple-darwin PARENT_SCOPE)
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(${out_var} aarch64-unknown-linux-gnu PARENT_SCOPE)
    else()
        set(${out_var} x86_64-unknown-linux-gnu PARENT_SCOPE)
    endif()
endfunction()

deskhub_quiche_rust_target(DESKHUB_QUICHE_TARGET)

if(MSVC)
    set(DESKHUB_QUICHE_ARTIFACT quiche.lib)
else()
    set(DESKHUB_QUICHE_ARTIFACT libquiche.a)
endif()

set(DESKHUB_QUICHE_LIB
    ${DESKHUB_QUICHE_ROOT}/${DESKHUB_QUICHE_TARGET}/${DESKHUB_QUICHE_ARTIFACT})
set(DESKHUB_QUICHE_INCLUDE ${DESKHUB_QUICHE_ROOT}/include)

if(EXISTS ${DESKHUB_QUICHE_LIB} AND EXISTS ${DESKHUB_QUICHE_INCLUDE}/quiche.h
   AND EXISTS ${DESKHUB_QUICHE_INCLUDE}/openssl/x509.h)
    set(DESKHUB_QUICHE_FOUND ON)
else()
    set(DESKHUB_QUICHE_FOUND OFF)
endif()

if(DESKHUB_QUICHE_FOUND AND NOT TARGET deskhub::quiche)
    add_library(deskhub::quiche STATIC IMPORTED GLOBAL)
    set_target_properties(deskhub::quiche PROPERTIES
        IMPORTED_LOCATION ${DESKHUB_QUICHE_LIB}
        INTERFACE_INCLUDE_DIRECTORIES ${DESKHUB_QUICHE_INCLUDE})

    if(WIN32)
        set_property(TARGET deskhub::quiche APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            ws2_32 userenv advapi32 bcrypt ntdll crypt32)
    elseif(APPLE)
        set_property(TARGET deskhub::quiche APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            "-framework Security" "-framework CoreFoundation")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
        set_property(TARGET deskhub::quiche APPEND PROPERTY INTERFACE_LINK_LIBRARIES log)
    else()
        find_package(Threads REQUIRED)
        set_property(TARGET deskhub::quiche APPEND PROPERTY INTERFACE_LINK_LIBRARIES
            Threads::Threads ${CMAKE_DL_LIBS} m)
    endif()
endif()
