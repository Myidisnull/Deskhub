#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Viewer.h"

#include <windows.h>
#include <algorithm>
#include <memory>
#include <mutex>

#include "deskhubp/ffi/ClientSession.h"
#include "ViewerInput.h"

namespace {

constexpr wchar_t kFrameClass[] = L"DeskhubViewerFrame";
constexpr wchar_t kVideoClass[] = L"DeskhubViewerVideo";

constexpr UINT WM_APP_STATS = WM_APP + 1;
constexpr UINT WM_APP_SIZE = WM_APP + 2;
constexpr UINT WM_APP_CLOSED = WM_APP + 3;
constexpr UINT kTimerHint = 1;

int g_openFrames = 0;

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}

struct ViewerFrame {
    HWND hwnd = nullptr;
    HWND video = nullptr;
    DHSession* session = nullptr;
    ViewerInput input;
    std::wstring baseTitle;
    std::wstring shownTitle;
    bool sizedToVideo = false;

    std::mutex mu;
    std::string statsLine;
    std::string closedReason;
    uint32_t videoW = 0, videoH = 0;

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
        const double scale = std::min(double(aw) / w, double(ah) / h);
        const int vw = std::max(1, int(w * scale)), vh = std::max(1, int(h * scale));
        MoveWindow(video, (aw - vw) / 2, (ah - vh) / 2, vw, vh, TRUE);
    }

    void UpdateTitle() {
        std::string line;
        {
            std::lock_guard<std::mutex> lk(mu);
            line = statsLine;
        }
        const std::wstring stats = line.empty() ? L"connecting..." : FromUtf8(line);
        const wchar_t* hint = input.relativeMode() ? L"Mouse locked - press F9 to release"
                                                   : L"Press F9 to lock mouse";
        std::wstring t = baseTitle + L" — " + stats + L" · " + hint;
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
        if (!w || !h || sizedToVideo) return;
        sizedToVideo = true;
        RECT wa{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        RECT wr{0, 0, LONG(w), LONG(h)};
        AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
        const int ww = std::min<int>(wr.right - wr.left, wa.right - wa.left - 48);
        const int wh = std::min<int>(wr.bottom - wr.top, wa.bottom - wa.top - 48);
        SetWindowPos(hwnd, nullptr, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER);
    }
};

LRESULT CALLBACK VideoProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* f = (ViewerFrame*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (msg == WM_LBUTTONDOWN) SetFocus(h);
    if (f && f->input.OnMessage(h, msg, wp, lp)) return 0;
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK FrameProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* f = (ViewerFrame*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
        case WM_SIZE:
            if (f) f->Relayout();
            return 0;
        case WM_TIMER:
            if (f && wp == kTimerHint) f->UpdateTitle();
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
            const std::wstring msgText =
                L"Connection ended: " + FromUtf8(reason.empty() ? "disconnected" : reason);
            MessageBoxW(h, msgText.c_str(), L"Deskhub", MB_OK | MB_ICONINFORMATION);
            DestroyWindow(h);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            if (f) f->input.Detach();
            if (--g_openFrames <= 0) PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void RegisterClasses() {
    static bool done = false;
    if (done) return;
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
    done = true;
}

std::unique_ptr<ViewerFrame> OpenFrame(const std::string& addr, uint8_t sourceId,
    const std::string& nameUtf8) {
    auto f = std::make_unique<ViewerFrame>();

    f->baseTitle = L"Deskhub - viewing";
    if (!nameUtf8.empty()) f->baseTitle += L": " + FromUtf8(nameUtf8);

    f->hwnd = CreateWindowExW(0, kFrameClass, f->baseTitle.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 600, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!f->hwnd) return nullptr;
    SetWindowLongPtrW(f->hwnd, GWLP_USERDATA, (LONG_PTR)f.get());

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
            fr->closedReason = reason ? reason : "disconnected";
        }
        PostMessageW(fr->hwnd, WM_APP_CLOSED, 0, 0);
    };

    f->session = dh_session_start(addr.c_str(), sourceId, f->video, &callbacks);
    if (!f->session) {
        DestroyWindow(f->hwnd);
        return nullptr;
    }

    f->input.Attach(f->video, f->session);

    ++g_openFrames;
    SetTimer(f->hwnd, kTimerHint, 500, nullptr);
    f->UpdateTitle();
    f->Relayout();
    ShowWindow(f->hwnd, SW_SHOW);
    SetFocus(f->video);
    return f;
}

}

void RunViewer(const std::string& addrUtf8, const std::vector<deskhub::SourceInfo>& sources) {
    RegisterClasses();
    g_openFrames = 0;

    std::vector<std::unique_ptr<ViewerFrame>> frames;
    if (sources.empty()) {
        if (auto f = OpenFrame(addrUtf8, 0, "")) frames.push_back(std::move(f));
    } else {
        for (const auto& s : sources)
            if (auto f = OpenFrame(addrUtf8, s.sourceId, s.name))
                frames.push_back(std::move(f));
    }
    if (frames.empty()) {
        MessageBoxW(nullptr,
            L"Could not open a viewing session - check the address and that the other machine "
            L"is sharing.",
            L"Deskhub", MB_OK | MB_ICONWARNING);
        return;
    }

    MSG msg;
    BOOL got;
    while ((got = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (got == -1) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    for (auto& f : frames)
        if (f->session) dh_session_stop(f->session);
}
