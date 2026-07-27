// =============================================================================
// Viewer.cpp — cài đặt cửa sổ xem (xem vai trò + mô hình ở Viewer.h).
//
// BỐ CỤC MỘT CỬA SỔ
//   [thanh trên cao 34px: stats (trái) · gợi ý F9 · nút Disconnect (phải)]
//   [vùng video nền đen: cửa sổ CON aspect-fit — letterbox nằm ngoài cửa sổ con
//    nên toạ độ chuột chuẩn hoá theo client rect của con là đúng khung video]
//
// KÍCH THƯỚC BAN ĐẦU THEO VIDEO
//   sizeCb đầu tiên báo WxH đàm phán được -> nới cửa sổ cho vùng video đúng tỷ
//   lệ (kẹp vào vùng làm việc). Sau đó người dùng kéo cỡ tuỳ ý, video tự fit.
//
// KHUÔN MẪU WIN32 giống MainMenuWindow.cpp: control tạo tay, id số nguyên,
// WndProc rẽ nhánh WM_COMMAND; mọi cửa sổ chạy trên MỘT thread (thread gọi
// RunViewer) — phiên dh_client tự có thread recv/decode riêng của nó.
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Viewer.h"

#include <windows.h>
#include <algorithm>
#include <memory>
#include <mutex>

#include "DeskhubApi.h"
#include "ViewerInput.h"

namespace {

constexpr wchar_t kFrameClass[] = L"DeskhubViewerFrame";
constexpr wchar_t kVideoClass[] = L"DeskhubViewerVideo";

constexpr int kBarH = 34;
constexpr int kIdDisconnect = 400;
constexpr int kIdStats = 401;
constexpr int kIdHint = 402;

constexpr UINT WM_APP_STATS = WM_APP + 1;  // statsCb có dòng mới
constexpr UINT WM_APP_SIZE = WM_APP + 2;   // sizeCb báo cỡ video (lần đầu/đổi)
constexpr UINT WM_APP_CLOSED = WM_APP + 3; // phiên đứt từ phía native
constexpr UINT kTimerHint = 1;             // cập nhật chữ F9 theo trạng thái khoá

int g_openFrames = 0; // đếm cửa sổ đang mở; về 0 là RunViewer kết thúc

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}

// Một cửa sổ xem = một phiên dh_client. Sống từ Create tới WM_DESTROY; RunViewer
// giữ danh sách unique_ptr và dh_client_stop sau khi vòng message kết thúc.
struct ViewerFrame {
    HWND hwnd = nullptr;
    HWND video = nullptr; // cửa sổ con native render vào
    HWND stats = nullptr;
    HWND hint = nullptr;
    DhClientHandle* client = nullptr;
    ViewerInput input;
    bool sizedToVideo = false; // đã nới cửa sổ theo cỡ video lần đầu chưa

    // Thread nền của phiên ghi, thread UI đọc (dưới mu).
    std::mutex mu;
    std::string statsLine;
    std::string closedReason;
    uint32_t videoW = 0, videoH = 0;

    // Đặt cửa sổ con theo letterbox trong vùng dưới thanh bar.
    void Relayout() {
        uint32_t w, h;
        {
            std::lock_guard<std::mutex> lk(mu);
            w = videoW;
            h = videoH;
        }
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const int aw = rc.right, ah = rc.bottom - kBarH;
        if (!w || !h || aw <= 0 || ah <= 0) {
            MoveWindow(video, 0, kBarH, std::max(1, aw), std::max(1, ah), TRUE);
            return;
        }
        const double scale = std::min(double(aw) / w, double(ah) / h);
        const int vw = std::max(1, int(w * scale)), vh = std::max(1, int(h * scale));
        MoveWindow(video, (aw - vw) / 2, kBarH + (ah - vh) / 2, vw, vh, TRUE);
        // Thanh trên: stats co giãn, hint + nút neo phải.
        MoveWindow(stats, 8, 8, std::max(50, aw - 8 - 330), 18, TRUE);
        MoveWindow(hint, aw - 322, 8, 210, 18, TRUE);
        MoveWindow(GetDlgItem(hwnd, kIdDisconnect), aw - 104, 4, 96, 26, TRUE);
    }

    // Lần đầu biết cỡ video: nới cửa sổ để vùng video đúng 1:1 (kẹp vào work area).
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
        RECT wr{0, 0, LONG(w), LONG(h) + kBarH};
        AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
        const int ww = std::min<int>(wr.right - wr.left, wa.right - wa.left - 48);
        const int wh = std::min<int>(wr.bottom - wr.top, wa.bottom - wa.top - 48);
        SetWindowPos(hwnd, nullptr, 0, 0, ww, wh, SWP_NOMOVE | SWP_NOZORDER);
    }
};

// WndProc cửa sổ CON video: chuyển hết cho ViewerInput; bấm vào là lấy focus bàn
// phím (Raw Input chỉ tới cửa sổ đang focus).
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
            // Chữ gợi ý đổi theo trạng thái khoá — F9 được ViewerInput xử lý tại
            // chỗ nên frame chỉ việc đọc lại mỗi nhịp.
            if (f && wp == kTimerHint)
                SetWindowTextW(f->hint, f->input.relativeMode()
                                            ? L"Mouse locked - press F9 to release"
                                            : L"Press F9 to lock mouse");
            return 0;
        case WM_APP_STATS: {
            if (!f) return 0;
            std::string line;
            {
                std::lock_guard<std::mutex> lk(f->mu);
                line = f->statsLine;
            }
            SetWindowTextW(f->stats, FromUtf8(line).c_str());
            return 0;
        }
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
            // Phiên đứt từ phía native: báo ngắn rồi đóng cửa sổ — quay về menu.
            const std::wstring msgText =
                L"Connection ended: " + FromUtf8(reason.empty() ? "disconnected" : reason);
            MessageBoxW(h, msgText.c_str(), L"Deskhub", MB_OK | MB_ICONINFORMATION);
            DestroyWindow(h);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == kIdDisconnect) {
                DestroyWindow(h);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            // Thả khoá chuột TRƯỚC khi cửa sổ biến mất, không thì ClipCursor kẹt.
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

// Mở một cửa sổ xem + phiên dh_client cho `sourceId`. Trả nullptr nếu không mở
// được phiên (cửa sổ cũng bị huỷ theo).
std::unique_ptr<ViewerFrame> OpenFrame(const std::string& addr, uint8_t sourceId,
    const std::string& nameUtf8) {
    auto f = std::make_unique<ViewerFrame>();

    std::wstring title = L"Deskhub - viewing";
    if (!nameUtf8.empty()) title += L": " + FromUtf8(nameUtf8);

    f->hwnd = CreateWindowExW(0, kFrameClass, title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 600 + kBarH, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!f->hwnd) return nullptr;
    SetWindowLongPtrW(f->hwnd, GWLP_USERDATA, (LONG_PTR)f.get());

    const HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD s, int cx, int cy, int cw,
                  int ch, int id) {
        HWND c = CreateWindowExW(0, cls, text, s | WS_CHILD | WS_VISIBLE, cx, cy, cw, ch,
            f->hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
        if (c) SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        return c;
    };
    f->stats = mk(L"STATIC", L"connecting...", SS_LEFT | SS_ENDELLIPSIS, 8, 8, 400, 18,
        kIdStats);
    f->hint = mk(L"STATIC", L"Press F9 to lock mouse", SS_RIGHT, 440, 8, 210, 18, kIdHint);
    mk(L"BUTTON", L"Disconnect", BS_PUSHBUTTON, 660, 4, 96, 26, kIdDisconnect);
    f->video = CreateWindowExW(0, kVideoClass, L"", WS_CHILD | WS_VISIBLE, 0, kBarH, 16, 16,
        f->hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetWindowLongPtrW(f->video, GWLP_USERDATA, (LONG_PTR)f.get());

    // Ba callback chạy trên thread nền của phiên: chỉ ghi state + PostMessage.
    f->client = dh_client_start_hwnd(addr.c_str(), sourceId, (uint64_t)(uintptr_t)f->video, [](const char* line, void* user) {
            auto* fr = (ViewerFrame*)user;
            {
                std::lock_guard<std::mutex> lk(fr->mu);
                fr->statsLine = line;
            }
            PostMessageW(fr->hwnd, WM_APP_STATS, 0, 0); }, [](uint32_t w, uint32_t h, void* user) {
            auto* fr = (ViewerFrame*)user;
            {
                std::lock_guard<std::mutex> lk(fr->mu);
                fr->videoW = w;
                fr->videoH = h;
            }
            PostMessageW(fr->hwnd, WM_APP_SIZE, 0, 0); }, [](const char* reason, void* user) {
            auto* fr = (ViewerFrame*)user;
            {
                std::lock_guard<std::mutex> lk(fr->mu);
                fr->closedReason = reason ? reason : "disconnected";
            }
            PostMessageW(fr->hwnd, WM_APP_CLOSED, 0, 0); }, f.get());
    if (!f->client) {
        DestroyWindow(f->hwnd); // g_openFrames chưa tăng nên không đụng bộ đếm
        return nullptr;
    }

    f->input.Attach(f->video, f->client);

    ++g_openFrames;
    SetTimer(f->hwnd, kTimerHint, 500, nullptr);
    f->Relayout();
    ShowWindow(f->hwnd, SW_SHOW);
    SetFocus(f->video);
    return f;
}

} // namespace

void RunViewer(const std::string& addrUtf8, const std::vector<deskhub::SourceInfo>& sources) {
    RegisterClasses();
    g_openFrames = 0;

    std::vector<std::unique_ptr<ViewerFrame>> frames;
    if (sources.empty()) {
        // Host đời cũ không biết LIST_SOURCES: cứ xem nguồn 0 — phiên sẽ báo lỗi
        // cụ thể nếu địa chỉ sai, tốt hơn một hộp thoại "không thấy host".
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

    // dh_client_stop join thread phiên — làm SAU vòng message, cửa sổ đã đóng hết.
    for (auto& f : frames)
        if (f->client) dh_client_stop(f->client);
}
