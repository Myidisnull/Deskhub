cmake_minimum_required(VERSION 3.20)
project(DeskhubCore LANGUAGES CXX)

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

    src/control/BitrateController.cpp
    src/control/LinkStats.cpp
    src/control/StreamSize.cpp
    src/control/FrameGate.cpp
    src/control/ClockOffset.cpp
    src/control/QualityLadder.cpp

    src/media/ViewFit.cpp

    src/diag/ClientDiag.cpp
    src/diag/AgentDiag.cpp

    src/session/Beacon.cpp
)
target_compile_features(core PUBLIC cxx_std_20)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
    target_compile_options(core PRIVATE /W4 /permissive- /sdl)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(deskhub_tests_default OFF)
else()
    set(deskhub_tests_default ON)
endif()
option(DESKHUB_CORE_TESTS "Build core_tests" ${deskhub_tests_default})

if(DESKHUB_CORE_TESTS)
    add_executable(core_tests
        tests/TestMain.cpp
        tests/support/TestSupport.cpp

        tests/protocol/WireTests.cpp

        tests/transport/ReassemblerTests.cpp
        tests/transport/FecTests.cpp
        tests/transport/RetransmitCacheTests.cpp
        tests/transport/PacerTests.cpp

        tests/session/SessionTests.cpp
        tests/session/ClientPumpTests.cpp
        tests/session/HostFeedbackTests.cpp
        tests/session/HostRouterTests.cpp

        tests/input/InputTests.cpp
        tests/input/PressedInputTests.cpp
        tests/input/ClientInputQueueTests.cpp
        tests/input/ScancodeTableTests.cpp
        tests/input/PointerMapTests.cpp

        tests/control/ControlTests.cpp
        tests/control/StreamSizeTests.cpp
        tests/control/FrameGateTests.cpp
        tests/control/ClockOffsetTests.cpp
        tests/control/QualityLadderTests.cpp

        tests/diag/DiagTests.cpp

        tests/media/ContractTests.cpp
        tests/media/BitWriterTests.cpp
        tests/media/ViewFitTests.cpp

        tests/session/BeaconTests.cpp
    )
    target_link_libraries(core_tests PRIVATE core)
    target_include_directories(core_tests PRIVATE tests)
    if(MSVC)
        target_compile_options(core_tests PRIVATE /W4 /permissive-)
    endif()

    include(CTest)
    add_test(NAME core_tests COMMAND core_tests)
endif()
