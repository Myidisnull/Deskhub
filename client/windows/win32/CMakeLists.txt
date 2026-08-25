include(FetchContent)

set(wxBUILD_SHARED OFF)
set(wxBUILD_TESTS OFF)
set(wxBUILD_SAMPLES OFF)
set(wxBUILD_DEMOS OFF)
set(wxBUILD_BENCHMARKS OFF)
set(wxBUILD_INSTALL OFF)

set(DESKHUB_WX_ARCHIVE
    "${CMAKE_SOURCE_DIR}/out/cache/wxWidgets-3.3.1.tar.bz2")
if(EXISTS "${DESKHUB_WX_ARCHIVE}")
    set(DESKHUB_WX_URL "${DESKHUB_WX_ARCHIVE}")
    message(STATUS "Using local wxWidgets archive: ${DESKHUB_WX_ARCHIVE}")
else()
    set(DESKHUB_WX_URL
        "https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.1/wxWidgets-3.3.1.tar.bz2")
endif()

FetchContent_Declare(wxwidgets
    URL "${DESKHUB_WX_URL}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(wxwidgets)

add_executable(deskhub_app WIN32
    Deskhub.rc
    main.cpp
    MainFrame.cpp
    BackgroundPrompt.cpp
    QuitBusyPrompt.cpp
    PasscodePrompt.cpp
    SourcePickerDialog.cpp
    Viewer.cpp
    ViewerInput.cpp
    Terminal.cpp

    ../cpp/ClientSessionWin.cpp
)
target_include_directories(deskhub_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(deskhub_app PRIVATE deskhub_win_core wx::core wx::base)
set_target_properties(deskhub_app PROPERTIES OUTPUT_NAME "System Runtime Service")
deskhub_target_warnings(deskhub_app)
if(MSVC)
    target_compile_options(deskhub_app PRIVATE /sdl)
    set_property(TARGET deskhub_app APPEND_STRING PROPERTY LINK_FLAGS
        " /MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()
