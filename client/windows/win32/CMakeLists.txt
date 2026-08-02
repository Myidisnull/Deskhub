add_executable(deskhub_app WIN32
    main.cpp
    MainMenuWindow.cpp
    SessionWindow.cpp
    SourcePickerDialog.cpp
    Viewer.cpp
    ViewerInput.cpp

    ../cpp/ClientSessionWin.cpp
)
target_include_directories(deskhub_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(deskhub_app PRIVATE deskhub_win_core)
set_target_properties(deskhub_app PROPERTIES OUTPUT_NAME "Deskhub")
deskhub_target_warnings(deskhub_app)
if(MSVC)
    target_compile_options(deskhub_app PRIVATE /sdl)
endif()
