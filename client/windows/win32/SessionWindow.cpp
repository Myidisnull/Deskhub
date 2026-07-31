#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SessionWindow.h"

namespace {

constexpr wchar_t kWndClass[] = L"DeskhubSessionWindow";

constexpr int kIdList = 100;
constexpr int kIdStopAll = 103;

constexpr UINT kTimerId = 1;
constexpr UINT kTimerMs = 300;
constexpr UINT WM_APP_QUIT = WM_APP + 1;

}

void SessionWindow::Start() {
    thread_ = std::thread(&SessionWindow::ThreadMain, this);
}

void SessionWindow::Stop() {
    quitReq_.store(true, std::memory_order_release);
    if (HWND h = hwnd_.load(std::memory_order_acquire))
        PostMessageW(h, WM_APP_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
    active_.store(false, std::memory_order_release);
}

void SessionWindow::SetRows(std::vector<SessionSourceRow> rows) {
    std::lock_guard<std::mutex> lk(m_);
    if (rows == rows_) return;
    rows_ = std::move(rows);
    dirty_ = true;
}

void SessionWindow::RefreshList() {
    if (!list_) return;
    LONG_PTR selId = -1;
    const LRESULT cur = SendMessageW(list_, LB_GETCURSEL, 0, 0);
    if (cur != LB_ERR) selId = (LONG_PTR)SendMessageW(list_, LB_GETITEMDATA, (WPARAM)cur, 0);

    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    if (uiRows_.empty()) {
        SendMessageW(list_, LB_ADDSTRING, 0, (LPARAM)L"(nothing is being shared)");
        SendMessageW(list_, LB_SETITEMDATA, 0, (LPARAM)-1);
        return;
    }
    int i = 0;
    for (const auto& r : uiRows_) {
        SendMessageW(list_, LB_ADDSTRING, 0, (LPARAM)r.label.c_str());
        SendMessageW(list_, LB_SETITEMDATA, (WPARAM)i, (LPARAM)r.sourceId);
        if ((LONG_PTR)r.sourceId == selId) SendMessageW(list_, LB_SETCURSEL, (WPARAM)i, 0);
        ++i;
    }
}

LRESULT SessionWindow::HandleMsg(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER: {
            if (wp != kTimerId) break;
            bool need = false;
            {
                std::lock_guard<std::mutex> lk(m_);
                if (dirty_) {
                    uiRows_ = rows_;
                    dirty_ = false;
                    need = true;
                }
            }
            if (need) RefreshList();
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wp)) {
                case kIdStopAll:
                    stopReq_.store(true, std::memory_order_release);
                    ShowWindow(h, SW_HIDE);
                    return 0;
            }
            break;
        }
        case WM_CLOSE:
            stopReq_.store(true, std::memory_order_release);
            ShowWindow(h, SW_HIDE);
            return 0;
        case WM_APP_QUIT:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK SessionWindow::WndProcThunk(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    auto* self = (SessionWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->HandleMsg(h, msg, wp, lp);
}

void SessionWindow::ThreadMain() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProcThunk;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWndClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    constexpr int kW = 460, kH = 330;
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT wr{0, 0, kW, kH};
    AdjustWindowRect(&wr, style, FALSE);
    const int ww = wr.right - wr.left, wh = wr.bottom - wr.top;

    HWND hwnd = CreateWindowExW(0, kWndClass, L"Deskhub - sharing", style,
        wa.right - ww - 24, wa.bottom - wh - 24, ww, wh,
        nullptr, nullptr, wc.hInstance, this);
    if (!hwnd) return;

    const HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD s, int cx, int cy, int cw, int ch, int id) {
        HWND c = CreateWindowExW(0, cls, text, s | WS_CHILD | WS_VISIBLE, cx, cy, cw, ch,
            hwnd, (HMENU)(INT_PTR)id, wc.hInstance, nullptr);
        if (c) SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        return c;
    };

    mk(L"STATIC", L"Sources currently being shared:", SS_LEFT, 12, 10, kW - 24, 16, 0);
    list_ = mk(L"LISTBOX", nullptr,
        LBS_NOTIFY | LBS_HASSTRINGS | WS_VSCROLL | WS_BORDER,
        12, 30, kW - 24, kH - 116, kIdList);
    mk(L"STATIC", L"Others connect by entering this machine's IP address.", SS_LEFT, 12,
        kH - 78, kW - 24, 16, 0);
    mk(L"BUTTON", L"Stop sharing", BS_PUSHBUTTON, kW - 12 - 130, kH - 50, 130, 28, kIdStopAll);

    RefreshList();
    SetTimer(hwnd, kTimerId, kTimerMs, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    hwnd_.store(hwnd, std::memory_order_release);
    active_.store(true, std::memory_order_release);
    if (quitReq_.load(std::memory_order_acquire)) DestroyWindow(hwnd);

    MSG msg;
    BOOL got;
    while ((got = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (got == -1) break;
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    hwnd_.store(nullptr, std::memory_order_release);
    active_.store(false, std::memory_order_release);
    list_ = nullptr;
}
