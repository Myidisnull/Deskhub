cmake_minimum_required(VERSION 3.22)

file(READ ${CMAKE_CURRENT_LIST_DIR}/../VERSION DESKHUB_VERSION_TEXT)
string(STRIP "${DESKHUB_VERSION_TEXT}" DESKHUB_VERSION_TEXT)
project(DeskhubCore LANGUAGES C CXX VERSION ${DESKHUB_VERSION_TEXT})

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_LIST_DIR}/../VERSION)

if(NOT COMMAND deskhub_target_warnings)
    include(${CMAKE_CURRENT_LIST_DIR}/../cmake/DeskhubWarnings.cmake)
endif()

add_library(core STATIC
    src/protocol/Wire.cpp

    src/crypto/Keys.cpp
    src/crypto/Aead.cpp
    src/crypto/NoiseXx.cpp
    src/crypto/TrafficCipher.cpp
    src/crypto/KeyCodec.cpp
    third_party/monocypher/monocypher.c

    src/transport/Packetizer.cpp
    src/transport/Reassembler.cpp
    src/transport/RetransmitCache.cpp
    src/transport/AudioJitterBuffer.cpp

    src/session/HostSession.cpp
    src/session/HostViewers.cpp
    src/session/ClientSession.cpp
    src/session/ClientPump.cpp
    src/session/HostFeedback.cpp
    src/session/HostRouter.cpp
    src/session/ClipboardSync.cpp

    src/input/InputSender.cpp
    src/input/InputReceiver.cpp
    src/input/ClientInputQueue.cpp
    src/input/TrackpadCursor.cpp

    src/control/BitrateController.cpp
    src/control/LinkStats.cpp
    src/control/StreamSize.cpp
    src/control/FrameGate.cpp
    src/control/ClockOffset.cpp
    src/control/VideoPacer.cpp
    src/control/QualityLadder.cpp

    src/media/ViewFit.cpp
    src/media/H264Sps.cpp
    src/media/AnnexB.cpp
    src/media/RatePlan.cpp
    src/media/H264Encode.cpp
    src/media/RgbDownscale.cpp

    src/diag/ClientDiag.cpp
    src/diag/AgentDiag.cpp

    src/session/Beacon.cpp

    src/net/LanScan.cpp
    src/net/Ipv4.cpp
    src/net/BindAddress.cpp

    src/ui/HostRows.cpp
    src/ui/RecentDevices.cpp
    src/ui/SecretText.cpp
    src/ui/UiSettings.cpp
    src/ui/AutostartConfig.cpp
    src/ui/AutoShareGate.cpp
    src/ui/Locale.cpp
    src/ui/LocaleCatalog.cpp
)
target_compile_features(core PUBLIC cxx_std_20)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/monocypher)
target_compile_definitions(core PUBLIC DESKHUB_VERSION="${PROJECT_VERSION}")

set_source_files_properties(third_party/monocypher/monocypher.c PROPERTIES
    LANGUAGE C
    COMPILE_OPTIONS "$<$<C_COMPILER_ID:MSVC>:/wd4244>")
if(MSVC)
    set_source_files_properties(third_party/monocypher/monocypher.c PROPERTIES COMPILE_FLAGS "/wd4244")
endif()

deskhub_target_warnings(core)
if(MSVC)
    target_compile_options(core PRIVATE /sdl)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Android" OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(deskhub_tests_default OFF)
else()
    set(deskhub_tests_default ON)
endif()
option(DESKHUB_CORE_TESTS "Build core_tests" ${deskhub_tests_default})

if(DESKHUB_CORE_TESTS)
    add_executable(core_tests
        tests/TestMain.cpp
        tests/support/TestSupport.cpp

        tests/protocol/ByteOrderTests.cpp
        tests/protocol/WireTests.cpp

        tests/transport/ReassemblerTests.cpp
        tests/transport/FecTests.cpp
        tests/transport/RetransmitCacheTests.cpp
        tests/transport/PacerTests.cpp
        tests/transport/AudioJitterBufferTests.cpp

        tests/session/SessionTests.cpp
        tests/session/ClientPumpTests.cpp
        tests/session/ClientReconnectTests.cpp
        tests/session/ClipboardSyncTests.cpp
        tests/session/HostFeedbackTests.cpp
        tests/session/HostRouterTests.cpp
        tests/session/HostViewersTests.cpp
        tests/session/ConnectFlowTests.cpp
        tests/session/ShareFlowTests.cpp
        tests/session/OpenViewersTests.cpp
        tests/session/SourcePipelineStateTests.cpp
        tests/crypto/NoiseAeadTests.cpp

        tests/input/InputTests.cpp
        tests/input/PressedInputTests.cpp
        tests/input/ClientInputQueueTests.cpp
        tests/input/ScancodeTableTests.cpp
        tests/input/Set1ScancodeTests.cpp
        tests/input/HotkeysTests.cpp
        tests/input/PointerMapTests.cpp
        tests/input/PointerLockStateTests.cpp
        tests/input/TrackpadCursorTests.cpp

        tests/control/ControlTests.cpp
        tests/control/StreamSizeTests.cpp
        tests/control/FrameGateTests.cpp
        tests/control/ClockOffsetTests.cpp
        tests/control/VideoPacerTests.cpp
        tests/control/QualityLadderTests.cpp

        tests/diag/DiagTests.cpp
        tests/diag/LogPolicyTests.cpp

        tests/media/ContractTests.cpp
        tests/media/BitWriterTests.cpp
        tests/media/H264SpsTests.cpp
        tests/media/AnnexBTests.cpp
        tests/media/RatePlanTests.cpp
        tests/media/H264EncodeTests.cpp
        tests/media/QualityPresetTests.cpp
        tests/media/ViewFitTests.cpp
        tests/media/SourceLabelTests.cpp
        tests/media/ViewerTitleTests.cpp
        tests/media/FrameMailboxTests.cpp
        tests/media/RgbDownscaleTests.cpp

        tests/session/BeaconTests.cpp

        tests/net/LanScanTests.cpp
        tests/net/Ipv4Tests.cpp
        tests/net/BindAddressTests.cpp

        tests/ui/StringsTests.cpp
        tests/ui/LocaleTests.cpp
        tests/ui/HostRowsTests.cpp
        tests/ui/RecentDevicesTests.cpp
        tests/ui/SecretTextTests.cpp
        tests/ui/UiSettingsTests.cpp
        tests/ui/AutostartConfigTests.cpp
        tests/ui/AutoShareGateTests.cpp

        tests/fuzz/FuzzTests.cpp
    )
    target_link_libraries(core_tests PRIVATE core)
    target_include_directories(core_tests PRIVATE tests)
    deskhub_target_warnings(core_tests)
    if(MSVC)
        target_link_options(core_tests PRIVATE /STACK:16777216)
    endif()

    include(CTest)
    add_test(NAME core_tests COMMAND core_tests)
endif()

option(DESKHUB_FUZZ "Build libFuzzer fuzz targets (clang only)" OFF)

if(DESKHUB_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "DESKHUB_FUZZ requires clang (libFuzzer)")
    endif()

    set(DESKHUB_FUZZER_LINK -fsanitize=fuzzer)
    if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        file(GLOB DESKHUB_FUZZER_RUNTIMES
            /opt/homebrew/opt/llvm/lib/clang/*/lib/darwin/libclang_rt.fuzzer_osx.a
            /usr/local/opt/llvm/lib/clang/*/lib/darwin/libclang_rt.fuzzer_osx.a)
        list(SORT DESKHUB_FUZZER_RUNTIMES COMPARE NATURAL)
        list(POP_BACK DESKHUB_FUZZER_RUNTIMES DESKHUB_FUZZER_LINK)
        if(NOT DESKHUB_FUZZER_LINK)
            message(FATAL_ERROR
                "libFuzzer runtime not found - Apple clang does not ship one.\n"
                "  Install Homebrew LLVM: brew install llvm   (or: make bootstrap)")
        endif()
    endif()

    function(deskhub_fuzz_target name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE core)
        target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
        target_link_options(${name} PRIVATE ${DESKHUB_FUZZER_LINK})
        deskhub_target_warnings(${name})
    endfunction()
    deskhub_fuzz_target(fuzz_wire fuzz/FuzzWire.cpp)
    deskhub_fuzz_target(fuzz_annexb fuzz/FuzzAnnexB.cpp)
    deskhub_fuzz_target(fuzz_h264sps fuzz/FuzzH264Sps.cpp)
    deskhub_fuzz_target(fuzz_reassembler fuzz/FuzzReassembler.cpp)
    deskhub_fuzz_target(fuzz_session fuzz/FuzzSession.cpp)
    deskhub_fuzz_target(fuzz_uitext fuzz/FuzzUiText.cpp)
endif()
