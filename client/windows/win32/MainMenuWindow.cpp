#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "MainMenuWindow.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "deskhubp/session/AgentLoop.h"
#include "deskhubp/session/ConnectDriver.h"
#include "ElevatedShare.h"
#include "SessionWindow.h"
#include "SourcePickerDialog.h"
#include "Viewer.h"
#include "WinControls.h"
#include "WinText.h"
#include "deskhub/session/ShareFlow.h"
#include "deskhub/media/QualityPreset.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/media/DisplayEnum.h"
#include "net/Firewall.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhub/protocol/Wire.h"

namespace {

constexpr wchar_t kWndClass[] = L"DeskhubMainMenu";

constexpr int kIdEditFps = 199;
constexpr int kIdEditBitrate = 198;
constexpr int kIdComboQuality = 197;
constexpr int kIdShare = 201;
constexpr int kIdEditAddr = 202;
constexpr int kIdConnect = 203;
constexpr int kIdExit = 205;
constexpr int kIdCopyBase = 300;

constexpr UINT kMsgRunFunction = WM_APP + 1;

void PostFunction(HWND hwnd, std::function<void()> fn) {
    auto* heap = new std::function<void()>(std::move(fn));
    if (!PostMessageW(hwnd, kMsgRunFunction, 0, (LPARAM)heap)) delete heap;
}

constexpr AgentOptions kShareDefaults{};

uint32_t GetEditUint(HWND edit, uint32_t fallback) {
    wchar_t buf[16] = {};
    GetWindowTextW(edit, buf, 16);
    return deskhub::ui::ParsePositiveUint(ToUtf8(buf), fallback);
}

uint32_t GetComboMaxDim(HWND combo, uint32_t fallback) {
    const LRESULT sel = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) return fallback;
    return deskhub::media::QualityPresetMaxDim(size_t(sel), fallback);
}

void CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (text.empty() || !OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(h);
            if (SetClipboardData(CF_UNICODETEXT, h)) h = nullptr;
        }
        if (h) GlobalFree(h);
    }
    CloseClipboard();
}

struct MenuState {
    HWND hwnd = nullptr;
    HWND editFps = nullptr;
    HWND editBitrate = nullptr;
    HWND comboQuality = nullptr;
    HWND editAddr = nullptr;
    std::vector<std::wstring> copyIps;
    bool quit = false;
    deskhubp::ConnectDriver connectDriver;
};

void DoShare(MenuState& st) {
    const deskhub::ShareClampResult clamp =
        deskhub::ClampShareSources(deskhubp::ListDisplays());
    if (clamp.clamped)
        MessageBoxW(st.hwnd, FromUtf8(deskhub::ui::ShareClampWarning()).c_str(), L"Deskhub",
            MB_OK | MB_ICONWARNING);

    const std::vector<AgentSource>& sources = clamp.sources;
    if (sources.empty()) {
        const std::string err = deskhubp::ListDisplaysError();
        const std::wstring msg =
            err.empty() ? L"No display found to share." : FromUtf8(err);
        MessageBoxW(st.hwnd, msg.c_str(), L"Deskhub", MB_OK | MB_ICONWARNING);
        return;
    }

    AgentOptions ao;
    ao.fps = GetEditUint(st.editFps, kShareDefaults.fps);
    ao.bitrateMbps = GetEditUint(st.editBitrate, kShareDefaults.bitrateMbps);
    ao.maxDim = GetComboMaxDim(st.comboQuality, kShareDefaults.maxDim);

    if (!IsProcessElevated()) {
        const bool needFirewall = !HostFirewallRulePresent();
        bool cancelled = false;
        if (RelaunchElevatedShare(sources, ao, cancelled)) {
            st.quit = true;
            return;
        }
        std::wstring msg = cancelled
                               ? std::wstring(L"Continuing without administrator rights.\n\n")
                               : std::wstring(
                                     L"Could not restart as administrator. Sharing continues "
                                     L"without it.\n\n");
        if (needFirewall)
            msg +=
                L"- Windows Firewall may block the other machine from connecting. "
                L"If it cannot connect, allow Deskhub.exe through Windows Firewall for "
                L"the network you are on, or run this program as administrator once.\n\n";
        msg +=
            L"- Mouse/keyboard control will not reach apps that run as "
            L"administrator (games with anti-cheat, elevated tools). Everything "
            L"else still works.";
        MessageBoxW(st.hwnd, msg.c_str(), L"Deskhub", MB_OK | MB_ICONWARNING);
    }

    ShowWindow(st.hwnd, SW_HIDE);
    RunSharingSession(st.hwnd, sources, ao);
    ShowWindow(st.hwnd, SW_SHOW);
    SetForegroundWindow(st.hwnd);
}

void OnSourcesReady(MenuState& st, const std::string& addr,
    const deskhubp::ConnectOutcome& outcome);

void DoConnect(MenuState& st) {
    wchar_t buf[128] = {};
    GetWindowTextW(st.editAddr, buf, 128);
    const std::string addr = deskhub::ui::TrimAscii(ToUtf8(buf));
    if (addr.empty()) {
        MessageBoxW(st.hwnd, L"Enter the host machine's IP address first (e.g., 192.168.1.10).",
            L"Deskhub", MB_OK | MB_ICONWARNING);
        return;
    }

    NetAddr server{};
    if (!ParseNetAddr(addr, server)) {
        const std::wstring msg = L"Invalid address: \"" + FromUtf8(addr) + L"\"\n" +
                                 FromUtf8(deskhub::ui::InvalidAddressHint());
        MessageBoxW(st.hwnd, msg.c_str(), L"Deskhub", MB_OK | MB_ICONERROR);
        return;
    }

    const bool started = st.connectDriver.QueryAsync(server, [hwnd = st.hwnd](std::function<void()> fn) { PostFunction(hwnd, std::move(fn)); }, [&st, addr](const deskhubp::ConnectOutcome& outcome) { OnSourcesReady(st, addr, outcome); });
    if (started) EnableWindow(GetDlgItem(st.hwnd, kIdConnect), FALSE);
}

void OnSourcesReady(MenuState& st, const std::string& addr,
    const deskhubp::ConnectOutcome& outcome) {
    EnableWindow(GetDlgItem(st.hwnd, kIdConnect), TRUE);

    std::vector<deskhub::SourceInfo> picked;
    if (outcome.hasSources()) {
        if (!ShowSourcePickerDialog(st.hwnd, outcome.sources, picked)) return;
    } else {
        picked = deskhubp::DefaultViewTargets();
    }

    ShowWindow(st.hwnd, SW_HIDE);
    RunViewer(addr, picked);
    ShowWindow(st.hwnd, SW_SHOW);
    SetForegroundWindow(st.hwnd);
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = (MenuState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
        case WM_COMMAND: {
            if (!st) break;
            const int id = LOWORD(wp);
            if (id >= kIdCopyBase && id < kIdCopyBase + (int)st->copyIps.size()) {
                CopyTextToClipboard(st->hwnd, st->copyIps[size_t(id - kIdCopyBase)]);
                return 0;
            }
            switch (id) {
                case kIdShare: DoShare(*st); return 0;
                case kIdConnect: DoConnect(*st); return 0;
                case kIdExit: st->quit = true; return 0;
            }
            break;
        }
        case kMsgRunFunction: {
            const std::unique_ptr<std::function<void()>> fn((std::function<void()>*)lp);
            if (fn && *fn) (*fn)();
            return 0;
        }
        case WM_CLOSE:
            if (st) st->quit = true;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}

int RunMainMenuWindow() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWndClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    MenuState st;

    const auto addrs = ListLocalIPv4();
    const int nRows = addrs.empty() ? 1 : (int)addrs.size();

    constexpr int kW = 496;
    const int gx = 12;
    const int gw = kW - 24;
    const int ix = gx + 14;
    const int iw = gw - 28;
    const int rowH = 22;

    const int hostTop = 8;
    const int settingsRel = 44 + nRows * rowH + 8;
    const int shareRel = settingsRel + 34;
    const int hostH = shareRel + 32 + 12;
    const int clientTop = hostTop + hostH + 10;
    const int clientH = 100;
    const int exitY = clientTop + clientH + 14;
    const int kH = exitY + 44;

    const DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT wr{0, 0, kW, kH};
    AdjustWindowRect(&wr, style, FALSE);
    HWND hwnd = CreateWindowExW(0, kWndClass,
        FromUtf8(deskhub::ui::kAppTitle).c_str(), style, CW_USEDEFAULT,
        CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, wc.hInstance,
        nullptr);
    if (!hwnd) return 1;

    st.hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&st);

    const ChildControlFactory mk(hwnd, wc.hInstance);

    mk(L"BUTTON", L"Host mode - share an application on THIS machine", BS_GROUPBOX, gx, hostTop,
        gw, hostH, 0);

    mk(L"STATIC", FromUtf8(deskhub::ui::kHostIpIntro).c_str(), SS_LEFT, ix,
        hostTop + 22, iw, 16, 0);

    constexpr int kCopyW = 60, kCopyH = 20;
    const int copyX = gx + gw - 14 - kCopyW;
    if (addrs.empty()) {
        mk(L"STATIC", FromUtf8(deskhub::ui::kNoNetworkAddress).c_str(), SS_LEFT, ix,
            hostTop + 44, iw, 18, 0);
    } else {
        st.copyIps.reserve(addrs.size());
        int i = 0;
        for (const auto& a : addrs) {
            const int rowY = hostTop + 44 + i * rowH;
            std::wstring addr(a.ip.begin(), a.ip.end());
            wchar_t line[192];
            swprintf(line, 192, L"%-20ls %ls", FromUtf8(a.name).c_str(), addr.c_str());
            mk(L"STATIC", line, SS_LEFT | SS_ENDELLIPSIS, ix, rowY + 2, copyX - 8 - ix, 18, 0);
            mk(L"BUTTON", L"Copy", BS_PUSHBUTTON, copyX, rowY, kCopyW, kCopyH, kIdCopyBase + i);
            st.copyIps.push_back(std::move(addr));
            ++i;
        }
    }

    const int sy = hostTop + settingsRel;
    mk(L"STATIC", FromUtf8(deskhub::ui::UdpPortLine()).c_str(), SS_LEFT, ix, sy + 3, 100, 18,
        0);
    mk(L"STATIC", L"FPS", SS_LEFT, ix + 104, sy + 3, 26, 18, 0);
    st.editFps = mk(L"EDIT", L"60", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, ix + 132, sy, 44, 24,
        kIdEditFps);
    mk(L"STATIC", L"Bitrate (Mbps)", SS_LEFT, ix + 182, sy + 3, 92, 18, 0);
    st.editBitrate = mk(L"EDIT", L"20", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, ix + 276, sy, 44,
        24, kIdEditBitrate);
    mk(L"STATIC", L"Quality", SS_LEFT, ix + 326, sy + 3, 48, 18, 0);
    st.comboQuality = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, ix + 376, sy, 68, 140,
        kIdComboQuality);
    if (st.comboQuality) {
        for (const auto& preset : deskhub::media::kQualityPresets) {
            const std::wstring label = FromUtf8(preset.label);
            SendMessageW(st.comboQuality, CB_ADDSTRING, 0, (LPARAM)label.c_str());
        }
        SendMessageW(st.comboQuality, CB_SETCURSEL,
            (WPARAM)deskhub::media::QualityPresetIndex(kShareDefaults.maxDim), 0);
    }

    mk(L"BUTTON", FromUtf8(deskhub::ui::kShareButton).c_str(), BS_PUSHBUTTON, ix,
        hostTop + shareRel, iw, 32, kIdShare);

    mk(L"BUTTON", L"Client mode - connect to ANOTHER machine", BS_GROUPBOX, gx, clientTop, gw,
        clientH, 0);

    mk(L"STATIC", FromUtf8(deskhub::ui::kClientIpPrompt).c_str(), SS_LEFT, ix, clientTop + 24,
        iw, 16, 0);
    st.editAddr =
        mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, ix, clientTop + 44, iw - 110, 26,
            kIdEditAddr);
    mk(L"BUTTON", L"Connect", BS_DEFPUSHBUTTON, ix + iw - 100, clientTop + 44, 100, 26,
        kIdConnect);

    mk(L"BUTTON", L"Exit", BS_PUSHBUTTON, gx, exitY, 100, 28, kIdExit);

    ShowWindow(hwnd, SW_SHOW);

    PumpMessagesUntil(hwnd, [&st] { return !st.quit; });

    DestroyWindow(hwnd);
    return 0;
}
