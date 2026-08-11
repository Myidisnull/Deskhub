include(FetchContent)

set(wxBUILD_SHARED OFF)
set(wxBUILD_TESTS OFF)
set(wxBUILD_SAMPLES OFF)
set(wxBUILD_DEMOS OFF)
set(wxBUILD_BENCHMARKS OFF)
set(wxBUILD_INSTALL OFF)

FetchContent_Declare(wxwidgets
    URL https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.1/wxWidgets-3.3.1.tar.bz2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(wxwidgets)

add_executable(deskhub_app WIN32
    Deskhub.rc
    main.cpp
    MainFrame.cpp
    PasscodePrompt.cpp
    SourcePickerDialog.cpp
    Viewer.cpp
    ViewerInput.cpp

    ../cpp/ClientSessionWin.cpp
)
target_include_directories(deskhub_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(deskhub_app PRIVATE deskhub_win_core wx::core wx::base)
set_target_properties(deskhub_app PROPERTIES OUTPUT_NAME "Deskhub")
deskhub_target_warnings(deskhub_app)
if(MSVC)
    target_compile_options(deskhub_app PRIVATE /sdl)
    set_property(TARGET deskhub_app APPEND_STRING PROPERTY LINK_FLAGS
        " /MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()
