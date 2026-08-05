#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <clocale>

#include "MainFrame.h"
#include "capture/ScreenCapture.h"
#include "deskhubp/diag/LogFile.h"

#pragma comment(linker,                                                              \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' "   \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
    "language='*'\"")

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    deskhubp::StartProcessLog();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::setlocale(LC_ALL, ".UTF8");
    capture::InitRuntime();

    return RunDeskhubApp();
}
