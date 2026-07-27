# client/windows/win32 — APP WINDOWS (Win32 thuần), frontend chính.
#
# KHÔI PHỤC TỪ LỊCH SỬ (2026-07-27): bộ UI Win32 gốc trước M4b (ff454de~1) —
# main menu + picker + cửa sổ phiên — giao diện utility đơn giản, control chuẩn
# Windows, MỘT exe không dependency. Vai host nói chuyện THẲNG với AgentLoop
# (như bản gốc); vai client viết mới mỏng trên C API (Viewer.cpp +
# dh_client_start_hwnd) vì ClientLoop cũ đã dời vào ClientApi.cpp.
#
# ClientApi.cpp compile thẳng vào exe — vai client headless nằm trọn trong đó.

add_executable(deskhub_app WIN32
    main.cpp
    MainMenuWindow.cpp
    SessionWindow.cpp
    SourcePickerDialog.cpp
    Viewer.cpp
    ViewerInput.cpp
    ScreenPickerDialog.cpp

    ../cpp/ClientApi.cpp
)
target_include_directories(deskhub_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(deskhub_app PRIVATE deskhub_win_core)
set_target_properties(deskhub_app PROPERTIES OUTPUT_NAME "Deskhub")
if(MSVC)
    target_compile_options(deskhub_app PRIVATE /W3 /permissive- /sdl)
endif()
