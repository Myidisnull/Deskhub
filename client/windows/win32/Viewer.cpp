#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Viewer.h"

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "deskhub/input/PointerLockState.h"
#include "deskhub/media/ViewFit.h"
#include "deskhub/media/ViewerTitle.h"
#include "deskhub/session/OpenViewers.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/ffi/ClientSession.h"
#include "deskhubp/ffi/ClientFfi.h"
#include "deskhubp/system/UiSettingsStore.h"
#include "AppIcon.h"
#include "ViewerInput.h"
#include "WinControls.h"
#include "WinText.h"

namespace {

constexpr wchar_t kFrameClass[] = L"SystemMonitorViewerFrame";
constexpr wchar_t kVideoClass[] = L"SystemMonitorViewerVideo";

constexpr UINT WM_APP_STATS = WM_APP + 1;
constexpr UINT WM_APP_SIZE = WM_APP + 2;
constexpr UINT WM_APP_CLOSED = WM_APP + 3;
constexpr UINT kTimerHint = 1;
constexpr UINT kTimerClipboard = 2;
constexpr UINT kIdSendFiles = 4001;

std::string ReadClipboardText(HWND owner) {
    if (!OpenClipboard(owner)) return {};
    std::string out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = ToUtf8(w);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

void WriteClipboardText(HWND owner, const std::string& utf8) {
    const std::wstring wide = FromUtf8(utf8);
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* dst = GlobalLock(mem)) {
            std::memcpy(dst, wide.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

std::vector<std::string> PickFilesToSend(HWND owner) {
    std::wstring buffer(32768, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = DWORD(buffer.size());
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT |
                OFN_HIDEREADONLY;
    ofn.lpstrTitle = L"Send files";
    if (!GetOpenFileNameW(&ofn)) return {};

    std::vector<std::string> paths;
    const wchar_t* p = buffer.c_str();
    const std::wstring first = p;
    p += first.size() + 1;
    if (*p == L'\0') {
        paths.push_back(ToUtf8(first));
        return paths;
    }
    const std::wstring dir = first;
    while (*p) {
        const std::wstring name = p;
        paths.push_back(ToUtf8(dir + L'\\' + name));
        p += name.size() + 1;
    }
    return paths;
}

void SendPickedFiles(DHSession* session, HWND owner) {
    if (!session) return;
    if (dh_session_file_busy(session)) return;
    const std::vector<std::string> paths = PickFilesToSend(owner);
    if (paths.empty()) return;
    std::vector<const char*> ptrs;
    ptrs.reserve(paths.size());
    for (const std::string& path : paths) ptrs.push_back(path.c_str());
    if (dh_session_file_send(session, ptrs.data(), int(ptrs.size()))) return;
    const char* err = dh_session_file_error(session);
    MessageBoxW(owner, FromUtf8(err && *err ? err : "Could not send files.").c_str(),
        FromUtf8(deskhub::ui::kAppTitle).c_str(), MB_OK | MB_ICONWARNING);
}

struct ViewerFrame {
    HWND hwnd = nullptr;
    HWND video = nullptr;
    DHSession* session = nullptr;
    deskhub::OpenViewerCount* openCount = nullptr;
    bool viewOnly = false;
    ViewerInput input;
    std::string baseTitle;
    std::wstring shownTitle;
    uint32_t fittedW = 0, fittedH = 0;

    std::mutex mu;
    std::string statsLine;
    std::string closedReason;
    uint32_t videoW = 0, videoH = 0;
    deskhub::ViewTransform transform{};
    bool panning = false;
    POINT panLast{};

    void Relayout() {
        uint32_t w, h;
        {
            std::lock_guard<std::mutex> lk(mu);
            w = videoW;
            h = videoH;
        }
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int aw = rc.right, ah = rc.bottom;
        if (!w || !h || aw <= 0 || ah <= 0) {
            MoveWindow(video, 0, 0, std::max(1, aw), std::max(1, ah), TRUE);
            return;
        }
        const deskhub::ViewRect r =
            deskhub::FitVideoRect(aw, ah, double(w) / double(h), transform);
        MoveWindow(video, int(r.x), int(r.y), std::max(1, int(r.width)),
            std::max(1, int(r.height)), TRUE);
    }

    void ApplyZoom(double factor, int centroidX, int centroidY) {
        uint32_t w, h;
        {
            std::lock_guard<std::mutex> lk(mu);
            w = videoW;
            h = videoH;
        }
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (!w || !h || rc.right <= 0 || rc.bottom <= 0) return;
        transform = deskhub::ApplyGesture(transform, factor, double(centroidX), double(centroidY),
            0, 0, double(rc.right), double(rc.bottom), double(w) / double(h));
        Relayout();
    }

    void ApplyPan(int dx, int dy) {
        uint32_t w, h;
        {
            std::lock_guard<std::mutex> lk(mu);
            w = videoW;
            h = videoH;
        }
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (!w || !h || rc.right <= 0 || rc.bottom <= 0) return;
        transform = deskhub::ApplyGesture(transform, 1.0, 0, 0, double(dx), double(dy),
            double(rc.right), double(rc.bottom), double(w) / double(h));
        Relayout();
    }

    void ResetZoom() {
        transform = {};
        Relayout();
    }

    void UpdateTitle() {
        std::string line;
        {
            std::lock_guard<std::mutex> lk(mu);
            line = statsLine;
        }
        if (session) {
            DHLinkHealth link{};
            dh_session_link_health(session, &link);
            if (link.quality != DHLinkUnknown || link.haveRtt) {
                std::string prefix = dh_link_quality_text(link.quality);
                if (link.haveRtt) prefix += " · " + std::to_string(link.rttMs) + " ms";
                if (!line.empty())
                    line = prefix + " · " + line;
                else
                    line = std::move(prefix);
            }
        }
        const std::string title =
            viewOnly
                ? deskhub::ComposeViewerTitle(baseTitle, line, deskhub::kViewerViewOnlyHint)
                : deskhub::PointerLockState(input.relativeMode()).TitleFor(baseTitle, line);
        std::wstring t = FromUtf8(title);
        if (t == shownTitle) return;
        shownTitle = std::move(t);
        SetWindowTextW(hwnd, shownTitle.c_str());
    }

    void SizeToVideo() {
        uint32_t w, h;
        {
            std::lock_guard<std::mutex> lk(mu);
            w = videoW;
            h = videoH;
        }
        if (!deskhub::ShouldRefitViewer(fittedW, fittedH, w, h)) return;
        fittedW = w;
        fittedH = h;
        RECT wa{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        RECT wr{0, 0, LONG(w), LONG(h)};
        AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
        const deskhub::ViewSize fitted = deskhub::ScaleToFit(uint32_t(wr.right - wr.left),
            uint32_t(wr.bottom - wr.top),
            uint32_t(std::max<LONG>(1, wa.right - wa.left - deskhub::kViewerMarginPx)),
            uint32_t(std::max<LONG>(1, wa.bottom - wa.top - deskhub::kViewerMarginPx)));
        SetWindowPos(hwnd, nullptr, 0, 0, int(fitted.width), int(fitted.height),
            SWP_NOMOVE | SWP_NOZORDER);
    }
};

LRESULT CALLBACK VideoProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* f = (ViewerFrame*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_LBUTTONDOWN) SetFocus(h);
    if (f && msg == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL)) {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(f->hwnd, &pt);
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        f->ApplyZoom(delta > 0 ? 1.1 : (1.0 / 1.1), pt.x, pt.y);
        return 0;
    }
    if (f && msg == WM_MBUTTONDOWN && deskhub::IsZoomed(f->transform.zoom)) {
        f->panning = true;
        f->panLast = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        SetCapture(h);
        return 0;
    }
    if (f && msg == WM_MOUSEMOVE && f->panning) {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        f->ApplyPan(x - f->panLast.x, y - f->panLast.y);
        f->panLast = {x, y};
        return 0;
    }
    if (f && (msg == WM_MBUTTONUP || msg == WM_CAPTURECHANGED) && f->panning) {
        f->panning = false;
        ReleaseCapture();
        return 0;
    }
    if (f && msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) < 0) && wp == '0') {
        f->ResetZoom();
        return 0;
    }
    if (f && msg == WM_KEYDOWN && (GetKeyState(VK_CONTROL) < 0) && wp == 'O') {
        SendPickedFiles(f->session, f->hwnd);
        return 0;
    }
    if (f && f->input.OnMessage(h, msg, wp, lp)) return 0;
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK FrameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* f = (ViewerFrame*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
        case WM_SIZE:
            if (f) f->Relayout();
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == kIdSendFiles && f) {
                SendPickedFiles(f->session, h);
                return 0;
            }
            break;
        case WM_SYSCOMMAND:
            if (wp == kIdSendFiles && f) {
                SendPickedFiles(f->session, h);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (f && (GetKeyState(VK_CONTROL) < 0) && wp == 'O') {
                SendPickedFiles(f->session, h);
                return 0;
            }
            break;
        case WM_TIMER:
            if (f && wp == kTimerHint) f->UpdateTitle();
            if (f && wp == kTimerClipboard && f->session) {
                char remote[deskhub::kMaxClipboardTextBytes + 1];
                if (dh_session_clip_take(f->session, remote, sizeof(remote)) > 0) {
                    WriteClipboardText(h, remote);
                } else {
                    const std::string local = ReadClipboardText(h);
                    if (!local.empty()) dh_session_clip_offer(f->session, local.c_str());
                }
            }
            return 0;
        case WM_APP_STATS:
            if (f) f->UpdateTitle();
            return 0;
        case WM_APP_SIZE:
            if (f) {
                f->SizeToVideo();
                f->Relayout();
            }
            return 0;
        case WM_APP_CLOSED: {
            if (!f) return 0;
            std::string reason;
            {
                std::lock_guard<std::mutex> lk(f->mu);
                reason = f->closedReason;
            }
            const std::wstring msgText = FromUtf8(deskhub::ui::kConnectionEndedTitle) + L": " +
                                         FromUtf8(reason.empty() ? deskhub::ui::kDisconnected.get()
                                                                 : reason.c_str());
            MessageBoxW(h, msgText.c_str(), FromUtf8(deskhub::ui::kAppTitle).c_str(),
                MB_OK | MB_ICONINFORMATION);
            DestroyWindow(h);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            if (f) {
                f->input.Detach();
                dh_viewer_closed();
                if (f->openCount && f->openCount->Closed()) PostQuitMessage(0);
            }
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void RegisterClasses() {
    static std::once_flag once;
    std::call_once(once, [] {
        WNDCLASSW wc{};
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

        wc.lpfnWndProc = FrameProc;
        wc.lpszClassName = kFrameClass;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc);

        wc.lpfnWndProc = VideoProc;
        wc.lpszClassName = kVideoClass;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc);
    });
}

std::unique_ptr<ViewerFrame> OpenFrame(const std::string& addr, uint8_t sourceId,
    const std::string& nameUtf8, deskhub::OpenViewerCount& openCount, bool control,
    const std::string& passcode, const std::string& sessionKey) {
    auto f = std::make_unique<ViewerFrame>();

    f->openCount = &openCount;
    f->viewOnly = !control;
    f->baseTitle = deskhub::ViewerBaseTitle(nameUtf8);

    const std::wstring initialTitle = FromUtf8(f->baseTitle);
    f->hwnd = CreateWindowExW(0, kFrameClass, initialTitle.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 600, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!f->hwnd) return nullptr;
    SetAppWindowIcon(f->hwnd);
    SetWindowLongPtrW(f->hwnd, GWLP_USERDATA, (LONG_PTR)f.get());
    if (HMENU sys = GetSystemMenu(f->hwnd, FALSE)) {
        AppendMenuW(sys, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(sys, MF_STRING, kIdSendFiles, FromUtf8(deskhub::ui::kSendFilesLabel).c_str());
    }

    f->video = CreateWindowExW(0, kVideoClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 16, 16,
        f->hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(f->video, GWLP_USERDATA, (LONG_PTR)f.get());

    DHSessionCallbacks callbacks{};
    callbacks.user = f.get();
    callbacks.onStatus = [](const char* line, void* user) {
        auto* fr = (ViewerFrame*)user;
        {
            std::lock_guard<std::mutex> lk(fr->mu);
            fr->statsLine = line;
        }
        PostMessageW(fr->hwnd, WM_APP_STATS, 0, 0);
    };
    callbacks.onSize = [](uint32_t w, uint32_t h, void* user) {
        auto* fr = (ViewerFrame*)user;
        {
            std::lock_guard<std::mutex> lk(fr->mu);
            fr->videoW = w;
            fr->videoH = h;
        }
        PostMessageW(fr->hwnd, WM_APP_SIZE, 0, 0);
    };
    callbacks.onClosed = [](const char* reason, void* user) {
        auto* fr = (ViewerFrame*)user;
        {
            std::lock_guard<std::mutex> lk(fr->mu);
            fr->closedReason = reason ? reason : deskhub::ui::kDisconnected.get();
        }
        PostMessageW(fr->hwnd, WM_APP_CLOSED, 0, 0);
    };

    f->session = dh_session_start(addr.c_str(), sourceId, f->video, &callbacks,
        passcode.c_str(), sessionKey.empty() ? nullptr : sessionKey.c_str());
    if (!f->session) {
        DestroyWindow(f->hwnd);
        return nullptr;
    }

    if (control) f->input.Attach(f->video, f->session);

    openCount.Opened();
    dh_viewer_opened();
    SetTimer(f->hwnd, kTimerHint, 500, nullptr);
    if (deskhubp::LoadUiSettings().clipboardSync)
        SetTimer(f->hwnd, kTimerClipboard, 1000, nullptr);
    f->UpdateTitle();
    f->Relayout();
    ShowWindow(f->hwnd, SW_SHOW);
    SetFocus(f->video);
    return f;
}

}

bool RunViewer(const std::string& addrUtf8, const std::vector<deskhub::SourceInfo>& sources,
    bool control, const std::string& passcode, const std::string& sessionKey) {
    RegisterClasses();
    deskhub::OpenViewerCount openFrames;

    std::vector<std::unique_ptr<ViewerFrame>> frames;
    for (const auto& s : sources)
        if (auto f = OpenFrame(addrUtf8, s.sourceId, s.name, openFrames, control, passcode,
                sessionKey))
            frames.push_back(std::move(f));
    if (frames.empty()) return false;

    PumpMessagesUntil(nullptr, [] { return true; });

    for (auto& f : frames)
        if (f->session) dh_session_stop(f->session);
    return true;
}
