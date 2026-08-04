cmake_minimum_required(VERSION 3.22)
project(DeskhubCore LANGUAGES CXX)

if(NOT COMMAND deskhub_target_warnings)
    include(${CMAKE_CURRENT_LIST_DIR}/../cmake/DeskhubWarnings.cmake)
endif()

add_library(core STATIC
    src/protocol/Wire.cpp

    src/transport/Packetizer.cpp
    src/transport/Reassembler.cpp
    src/transport/RetransmitCache.cpp

    src/session/HostSession.cpp
    src/session/ClientSession.cpp
    src/session/ClientPump.cpp
    src/session/HostFeedback.cpp
    src/session/HostRouter.cpp

    src/input/InputSender.cpp
    src/input/InputReceiver.cpp
    src/input/ClientInputQueue.cpp
    src/input/TrackpadCursor.cpp

    src/control/BitrateController.cpp
    src/control/LinkStats.cpp
    src/control/StreamSize.cpp
    src/control/FrameGate.cpp
    src/control/ClockOffset.cpp
    src/control/QualityLadder.cpp

    src/media/ViewFit.cpp
    src/media/H264Sps.cpp
    src/media/AnnexB.cpp
    src/media/RatePlan.cpp
    src/media/H264Encode.cpp

    src/diag/ClientDiag.cpp
    src/diag/AgentDiag.cpp

    src/session/Beacon.cpp
)
target_compile_features(core PUBLIC cxx_std_20)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

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

        tests/session/SessionTests.cpp
        tests/session/ClientPumpTests.cpp
        tests/session/HostFeedbackTests.cpp
        tests/session/HostRouterTests.cpp
        tests/session/ConnectFlowTests.cpp
        tests/session/ShareArgsTests.cpp
        tests/session/OpenViewersTests.cpp
        tests/session/SourcePipelineStateTests.cpp

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
        tests/control/QualityLadderTests.cpp

        tests/diag/DiagTests.cpp

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

        tests/session/BeaconTests.cpp

        tests/ui/StringsTests.cpp
    )
    target_link_libraries(core_tests PRIVATE core)
    target_include_directories(core_tests PRIVATE tests)
    deskhub_target_warnings(core_tests)

    include(CTest)
    add_test(NAME core_tests COMMAND core_tests)
endif()
