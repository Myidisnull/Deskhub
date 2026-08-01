cmake_minimum_required(VERSION 3.20)

add_executable(integration_tests
    TestMain.cpp
    WireVectorTests.cpp
    SessionFlowTests.cpp

    support/TestSupport.cpp
    support/FakeVideo.cpp
    support/FakeDecoder.cpp
    support/FakeAgent.cpp
)
target_link_libraries(integration_tests PRIVATE platform)
target_include_directories(integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(integration_tests PRIVATE cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(integration_tests PRIVATE Threads::Threads)

deskhub_target_warnings(integration_tests)

include(CTest)
add_test(NAME integration_tests COMMAND integration_tests)
set_tests_properties(integration_tests PROPERTIES TIMEOUT 900 RUN_SERIAL TRUE)
