#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Terminal.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "WinText.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/client/TerminalFeed.h"
#include "deskhubp/ffi/TermGridFill.h"
#include "deskhubp/ffi/TerminalFfi.h"
#include "deskhubp/host/TerminalHost.h"

namespace {

constexpr wchar_t kTermClass[] = L"SystemMonitorTerminal";
constexpr UINT kTimerPoll = 1;
constexpr int kCellW = 8;
constexpr int kCellH = 16;

struct TermUi {
    DHTermSession* session = nullptr;
    std::unique_ptr<deskhubp::LocalTerminalFeed> local{};
    std::vector<DHTermCell> cells;
    DHTermGrid grid{};
    HFONT font = nullptr;
    std::wstring status;
    bool quitOnDestroy = true;
};

TermUi* Ui(HWND hwnd) {
    return reinterpret_cast<TermUi*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void SetStatus(HWND hwnd, const std::wstring& line) {
    if (TermUi* ui = Ui(hwnd)) ui->status = line;
    InvalidateRect(hwnd, nullptr, FALSE);
}

void ResizeFeed(TermUi* ui, int cols, int rows) {
    if (!ui) return;
    const deskhub::TermSize size{uint16_t(std::max(1, cols)), uint16_t(std::max(1, rows))};
    if (ui->session) dh_term_resize(ui->session, size.cols, size.rows);
    if (ui->local) ui->local->Resize(size);
}

void PollGrid(HWND hwnd) {
    TermUi* ui = Ui(hwnd);
    if (!ui) return;

    if (ui->local) {
        if (!ui->local->Alive()) {
            SetStatus(hwnd, FromUtf8(deskhub::ui::kTerminalClosed));
            return;
        }
        const deskhub::term::TerminalSnapshot shot = ui->local->Snapshot(0);
        const uint32_t count = uint32_t(shot.size.rows) * uint32_t(shot.size.cols);
        ui->cells.assign(size_t(std::max(count, 1u)), DHTermCell{});
        deskhubp::FillTermGrid(shot, ui->cells.data(), uint32_t(ui->cells.size()), &ui->grid);
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }

    if (!ui->session) return;
    DHTermGrid grid{};
    if (!dh_term_grid(ui->session, 0, nullptr, 0, &grid)) return;
    const uint32_t count = uint32_t(grid.rows) * uint32_t(grid.cols);
    ui->cells.assign(size_t(count), DHTermCell{});
    if (count > 0) dh_term_grid(ui->session, 0, ui->cells.data(), count, &grid);
    ui->grid = grid;
    char msg[512];
    const int n = dh_term_message(ui->session, msg, int(sizeof(msg)));
    if (n > 0) SetStatus(hwnd, FromUtf8(std::string(msg, size_t(std::min(n, int(sizeof(msg) - 1))))));
    InvalidateRect(hwnd, nullptr, FALSE);
}

void SendKey(HWND hwnd, int32_t key, uint32_t codepoint = 0, bool ctrl = false) {
    TermUi* ui = Ui(hwnd);
    if (!ui) return;
    if (ui->session) {
        dh_term_send_key(ui->session, key, codepoint, false, false, ctrl);
        return;
    }
    if (!ui->local) return;
    deskhub::term::TermKeyEvent event{};
    if (!deskhubp::DecodeTermKey(key, codepoint, false, false, ctrl, event)) return;
    ui->local->SendKey(event);
}

LRESULT CALLBACK TermProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* ui = static_cast<TermUi*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ui));
            ui->font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN,
                L"Consolas");
            RECT rc{};
            GetClientRect(hwnd, &rc);
            ResizeFeed(ui, int(rc.right / kCellW), int(rc.bottom / kCellH));
            SetTimer(hwnd, kTimerPoll, 33, nullptr);
            SetFocus(hwnd);
            return 0;
        }
        case WM_TIMER:
            if (wp == kTimerPoll) PollGrid(hwnd);
            return 0;
        case WM_SIZE: {
            if (TermUi* ui = Ui(hwnd))
                ResizeFeed(ui, int(LOWORD(lp) / kCellW), int(HIWORD(lp) / kCellH));
            return 0;
        }
        case WM_KEYDOWN: {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            switch (wp) {
                case VK_RETURN: SendKey(hwnd, DHTermKeyEnter); return 0;
                case VK_BACK: SendKey(hwnd, DHTermKeyBackspace); return 0;
                case VK_TAB: SendKey(hwnd, DHTermKeyTab); return 0;
                case VK_ESCAPE: SendKey(hwnd, DHTermKeyEscape); return 0;
                case VK_UP: SendKey(hwnd, DHTermKeyUp); return 0;
                case VK_DOWN: SendKey(hwnd, DHTermKeyDown); return 0;
                case VK_LEFT: SendKey(hwnd, DHTermKeyLeft); return 0;
                case VK_RIGHT: SendKey(hwnd, DHTermKeyRight); return 0;
                case VK_HOME: SendKey(hwnd, DHTermKeyHome); return 0;
                case VK_END: SendKey(hwnd, DHTermKeyEnd); return 0;
                case VK_PRIOR: SendKey(hwnd, DHTermKeyPageUp); return 0;
                case VK_NEXT: SendKey(hwnd, DHTermKeyPageDown); return 0;
                case VK_DELETE: SendKey(hwnd, DHTermKeyDelete); return 0;
                default: break;
            }
            const int ch = MapVirtualKeyW(UINT(wp), MAPVK_VK_TO_CHAR);
            if (ch > 0) {
                SendKey(hwnd, DHTermKeyChar, uint32_t(ch), ctrl);
                return 0;
            }
            return 0;
        }
        case WM_CHAR: {
            if (wp >= 32) SendKey(hwnd, DHTermKeyChar, uint32_t(wp));
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            TermUi* ui = Ui(hwnd);
            if (ui && ui->font) {
                SelectObject(dc, ui->font);
                SetBkMode(dc, TRANSPARENT);
                for (uint16_t row = 0; row < ui->grid.rows; ++row) {
                    for (uint16_t col = 0; col < ui->grid.cols; ++col) {
                        const size_t idx = size_t(row) * ui->grid.cols + col;
                        if (idx >= ui->cells.size()) continue;
                        const DHTermCell& cell = ui->cells[idx];
                        const wchar_t ch = wchar_t(cell.codepoint < 32 ? L' ' : cell.codepoint);
                        SetTextColor(dc, RGB(cell.fgR, cell.fgG, cell.fgB));
                        SetBkColor(dc, RGB(cell.bgR, cell.bgG, cell.bgB));
                        TextOutW(dc, int(col) * kCellW, int(row) * kCellH, &ch, 1);
                    }
                }
                if (ui->grid.cursorVisible) {
                    RECT cursor{
                        LONG(ui->grid.cursorCol * kCellW), LONG(ui->grid.cursorRow * kCellH),
                        LONG((ui->grid.cursorCol + 1) * kCellW),
                        LONG((ui->grid.cursorRow + 1) * kCellH)};
                    InvertRect(dc, &cursor);
                }
            }
            if (ui && !ui->status.empty()) {
                SetTextColor(dc, RGB(160, 160, 160));
                TextOutW(dc, 4, rc.bottom - 18, ui->status.c_str(), int(ui->status.size()));
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, kTimerPoll);
            if (TermUi* ui = Ui(hwnd)) {
                const bool quit = ui->quitOnDestroy;
                if (ui->session) dh_term_stop(ui->session);
                if (ui->local) ui->local->Shutdown();
                if (ui->font) DeleteObject(ui->font);
                delete ui;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                if (quit) PostQuitMessage(0);
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool EnsureTermClass() {
    static bool registered = false;
    if (registered) return true;
    WNDCLASSW wc{};
    wc.lpfnWndProc = TermProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kTermClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    registered = RegisterClassW(&wc) != 0;
    return registered;
}

HWND OpenTermWindow(TermUi* ui, const std::wstring& title) {
    return CreateWindowExW(0, kTermClass, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, 900, 560, nullptr, nullptr, GetModuleHandleW(nullptr), ui);
}

}

bool RunTerminal(const std::string& addrUtf8, const std::string& passcode) {
    if (!EnsureTermClass()) return false;
    DHTermCallbacks callbacks{};
    DHTermSession* session =
        dh_term_open(addrUtf8.c_str(), passcode.c_str(), 100, 30, &callbacks);
    if (!session) return false;

    auto* ui = new TermUi;
    ui->session = session;

    HWND hwnd = OpenTermWindow(ui, FromUtf8(addrUtf8));
    if (!hwnd) {
        dh_term_stop(session);
        delete ui;
        return false;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return true;
}

bool RunLocalTerminal(deskhubp::TerminalHost& host, uint32_t termId) {
    if (!host.LocalAlive(termId)) return false;
    if (!EnsureTermClass()) return false;

    auto* ui = new TermUi;
    ui->local = std::make_unique<deskhubp::LocalTerminalFeed>(host, termId);
    ui->status = FromUtf8(deskhub::ui::kTerminalAttachedHere);
    ui->quitOnDestroy = false;

    HWND hwnd = OpenTermWindow(ui, FromUtf8(deskhub::ui::kTerminalLocalWindowTitle));
    if (!hwnd) {
        ui->local->Shutdown();
        delete ui;
        return false;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    return true;
}
