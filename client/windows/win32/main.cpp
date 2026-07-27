// =============================================================================
// main.cpp — điểm vào của app Windows (Win32 thuần). MỘT exe chứa CẢ HAI vai trò.
//
// KHÔI PHỤC TỪ LỊCH SỬ
//   Đây là bộ UI Win32 gốc (trước M4b), khôi phục từ commit ff454de~1 theo quyết
//   định 2026-07-27: giao diện utility đơn giản, control chuẩn của Windows, không
//   framework. Khác bản gốc đúng một chỗ: vai CLIENT nay đi qua C API
//   (dh_client_start_hwnd, xem Viewer.h) thay vì ClientLoop cũ đã gỡ.
//
// NHIỆM VỤ
//   Dựng môi trường tiến trình rồi giao quyền cho màn hình chính. Không có logic
//   nghiệp vụ — bốn việc khởi tạo và một đường rẽ:
//     1. StartProcessLog — log ra file cạnh exe (app GUI không có console).
//     2. DPI awareness — không có thì toạ độ chuột lệch trên máy scale ≠ 100%.
//     3. UTF-8 locale — in tiêu đề cửa sổ tiếng Việt vào log.
//     4. capture::InitRuntime — WinRT, bắt buộc trước khi dùng WGC.
//   Bình thường → RunMainMenuWindow(). Vừa được UAC nâng quyền → nhận nguồn qua
//   dòng lệnh, vào thẳng RunAgent rồi về màn hình chính (xem ElevatedShare.h).
//
// LIÊN QUAN: MainMenuWindow.h, SessionWindow.h, ElevatedShare.h, DiagLog.h
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <clocale>

#include <shellapi.h>
#include <vector>

#include "AgentLoop.h"
#include "DiagLog.h"
#include "ElevatedShare.h"
#include "MainMenuWindow.h"
#include "SessionWindow.h"
#include "capture/ScreenCapture.h"
#include "deskhub/protocol/Wire.h" // kMaxSources

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    StartProcessLog();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::setlocale(LC_ALL, ".UTF8");
    capture::InitRuntime();

    // Instance này có thể là bản vừa được UAC nâng quyền từ nút Share: nhận lại
    // nguồn + thông số qua dòng lệnh và vào thẳng phiên share, khỏi bắt người
    // dùng chọn nguồn lần hai. Xong phiên thì về main menu như chạy bình thường.
    int argc = 0;
    if (wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        std::vector<AgentSource> sources;
        AgentOptions opt;
        const bool elevatedShare = ParseElevatedShareArgs(argc, wargv, sources, opt);
        LocalFree(wargv);
        if (elevatedShare) {
            SessionWindow session;
            session.Start();
            RunAgent(sources, opt, session);
            session.Stop();
            return RunMainMenuWindow();
        }
    }

    return RunMainMenuWindow();
}
