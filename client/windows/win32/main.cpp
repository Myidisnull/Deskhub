#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <clocale>

#include <shellapi.h>
#include <vector>

#include "AgentLoop.h"
#include "deskhubp/diag/LogFile.h"
#include "ElevatedShare.h"
#include "MainMenuWindow.h"
#include "SessionWindow.h"
#include "capture/ScreenCapture.h"
#include "deskhub/protocol/Wire.h"

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    deskhubp::StartProcessLog();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::setlocale(LC_ALL, ".UTF8");
    capture::InitRuntime();

    int argc = 0;
    if (wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        std::vector<AgentSource> sources;
        AgentOptions opt;
        const bool elevatedShare = ParseElevatedShareArgs(argc, wargv, sources, opt);
        LocalFree(wargv);
        if (elevatedShare) {
            RunSharingSession(nullptr, sources, opt);
            return RunMainMenuWindow();
        }
    }

    return RunMainMenuWindow();
}
