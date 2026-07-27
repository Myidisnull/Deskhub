// =============================================================================
// ScreenPickerDialog.cpp — cài đặt hộp thoại chọn màn hình chia sẻ phía host.
//
// KHUÔN MẪU "HỘP THOẠI MODAL TỰ DỰNG"
//   Giống hệt SourcePickerDialog.cpp: tự tạo cửa sổ, vô hiệu hoá cửa sổ cha, chạy
//   vòng lặp message riêng tới khi xong. Xem giải thích đầy đủ ở file đó.
//
// HAI CHUỖI CHO MỘT MÀN HÌNH, đừng nhầm lẫn
//   Entry::label — hiện trong listbox, có kèm kích thước cho người dùng dễ nhận ra.
//   Entry::name  — tên GỬI CHO CLIENT, không kèm kích thước (client tự biết).
//   ToUtf8       — đổi name sang UTF-8 trước khi lên dây, vì client có thể không
//                  phải máy Windows. Hàm đối xứng FromUtf8 ở SourcePickerDialog.cpp.
//
// LIÊN QUAN: ScreenPickerDialog.h (vai trò + lý do gộp checkbox),
//            capture/DisplayFinder.h (nguồn danh sách), AgentLoop.h (AgentSource)
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ScreenPickerDialog.h"

#include <string>
#include <vector>

#include "capture/DisplayFinder.h"
#include "deskhub/protocol/Wire.h" // kMaxSources

namespace {

constexpr wchar_t kWndClass[] = L"DeskhubScreenPicker";

constexpr int kIdList = 100;
constexpr int kIdRefresh = 101;
constexpr int kIdChkAllow = 102;
constexpr int kIdOk = 103;
constexpr int kIdCancel = 104;
constexpr int kIdHint = 105;

// Tên nguồn đi trên dây là UTF-8 (client có thể không phải Windows).
std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()),
        nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// Một dòng trong danh sách: màn hình kèm nhãn hiển thị.
struct Entry {
    HMONITOR monitor = nullptr;
    std::wstring label; // hiện trong listbox
    std::wstring name;  // tên gửi cho client (không kèm kích thước)
};

std::vector<Entry> BuildEntries() {
    std::vector<Entry> entries;
    for (const auto& d : ListDisplays()) {
        wchar_t label[200];
        swprintf(label, 200, L"%ls (%ux%u)", d.name.c_str(), d.width, d.height);
        entries.push_back(Entry{d.monitor, label, d.name});
    }
    return entries;
}

struct PickerState {
    HWND hwnd = nullptr;
    HWND list = nullptr;
    HWND chkAllow = nullptr;
    std::vector<Entry> entries;
    std::vector<AgentSource> result;
    bool allowInput = true;
    bool done = false;
};

void Repopulate(PickerState& st) {
    st.entries = BuildEntries();
    SendMessageW(st.list, LB_RESETCONTENT, 0, 0);
    if (st.entries.empty()) {
        SendMessageW(st.list, LB_ADDSTRING, 0, (LPARAM)L"(No displays found)");
        EnableWindow(GetDlgItem(st.hwnd, kIdOk), FALSE);
        return;
    }
    for (const auto& e : st.entries)
        SendMessageW(st.list, LB_ADDSTRING, 0, (LPARAM)e.label.c_str());
    // Một màn hình thì chọn sẵn luôn — ca phổ biến nhất, đỡ một cú click. Nhiều màn
    // hình thì không chọn sẵn gì: buộc người dùng tự tick đúng thứ muốn chia sẻ
    // thay vì vô ý chia sẻ nhầm màn hình primary khi bấm Share ngay.
    if (st.entries.size() == 1) SendMessageW(st.list, LB_SETSEL, TRUE, 0);
    EnableWindow(GetDlgItem(st.hwnd, kIdOk), TRUE);
}

void Confirm(PickerState& st) {
    const LRESULT count = SendMessageW(st.list, LB_GETSELCOUNT, 0, 0);
    if (count <= 0 || st.entries.empty()) return; // chưa chọn gì

    std::vector<int> sel(static_cast<size_t>(count), 0);
    SendMessageW(st.list, LB_GETSELITEMS, WPARAM(count), (LPARAM)sel.data());

    st.result.clear();
    for (int i : sel) {
        if (i < 0 || size_t(i) >= st.entries.size()) continue;
        const Entry& e = st.entries[size_t(i)];
        st.result.push_back(AgentSource{e.monitor, ToUtf8(e.name)});
    }
    if (st.result.empty()) return;

    // Mỗi nguồn là một pipeline capture+encode riêng - quá nhiều thì GPU không kham
    // nổi và kMaxSources cũng là trần của SOURCE_LIST trong một datagram.
    if (st.result.size() > deskhub::kMaxSources) {
        wchar_t msg[160];
        swprintf(msg, 160, L"Please select at most %zu displays (you selected %zu).",
            deskhub::kMaxSources, st.result.size());
        MessageBoxW(st.hwnd, msg, L"Deskhub", MB_OK | MB_ICONWARNING);
        st.result.clear();
        return;
    }

    // Biến thể Add không có checkbox (chkAllow null) — giá trị không được dùng.
    if (st.chkAllow)
        st.allowInput = SendMessageW(st.chkAllow, BM_GETCHECK, 0, 0) == BST_CHECKED;
    st.done = true;
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = (PickerState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
        case WM_COMMAND:
            if (!st) break;
            switch (LOWORD(wp)) {
                case kIdRefresh: Repopulate(*st); return 0;
                case kIdOk: Confirm(*st); return 0;
                case kIdCancel: st->done = true; return 0;
                case kIdList:
                    if (HIWORD(wp) == LBN_DBLCLK) {
                        Confirm(*st);
                        return 0;
                    }
                    break;
            }
            break;
        case WM_CLOSE:
            if (st) st->done = true;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// Phần dùng chung của hai cửa ngõ public. `addMode` = mở từ nút Add của cửa sổ
// phiên: đổi tiêu đề + nhãn nút xác nhận và BỎ checkbox điều khiển (lý do ở
// ScreenPickerDialog.h).
bool RunPicker(HWND owner, bool addMode, std::vector<AgentSource>& outSources,
    bool* outAllowInput) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWndClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc); // lần 2 trả ALREADY_EXISTS - không sao

    constexpr int kW = 520, kH = 340;
    RECT ownerRect{};
    if (owner)
        GetWindowRect(owner, &ownerRect);
    else
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &ownerRect, 0);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - kW) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - kH) / 2;

    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT wr{0, 0, kW, kH};
    AdjustWindowRect(&wr, style, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kWndClass,
        addMode ? L"Add displays to the current share"
                : L"Select the display(s) to share",
        style, x, y, wr.right - wr.left, wr.bottom - wr.top,
        owner, nullptr, wc.hInstance, nullptr);
    if (!dlg) return false;

    PickerState st;
    st.hwnd = dlg;
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&st);

    const HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD s, int cx, int cy, int cw, int ch, int id) {
        HWND c = CreateWindowExW(0, cls, text, s | WS_CHILD | WS_VISIBLE, cx, cy, cw, ch,
            dlg, (HMENU)(INT_PTR)id, wc.hInstance, nullptr);
        if (c) SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        return c;
    };

    // LBS_MULTIPLESEL: click là bật/tắt một dòng. Dễ hiểu hơn LBS_EXTENDEDSEL
    // (đòi giữ Ctrl) với thao tác "tick những màn hình muốn chia sẻ".
    st.list = mk(L"LISTBOX", nullptr,
        LBS_NOTIFY | LBS_HASSTRINGS | LBS_MULTIPLESEL | WS_VSCROLL | WS_BORDER,
        12, 12, kW - 24, kH - 122, kIdList);
    mk(L"STATIC", L"Click each display you want to share (you can pick several).",
        0, 12, kH - 104, kW - 24, 18, kIdHint);
    if (!addMode) {
        st.chkAllow = mk(L"BUTTON", L"Allow the other person to control mouse/keyboard",
            BS_AUTOCHECKBOX, 12, kH - 82, kW - 24, 20, kIdChkAllow);
        SendMessageW(st.chkAllow, BM_SETCHECK, BST_CHECKED, 0);
    }
    mk(L"BUTTON", L"Refresh", 0, 12, kH - 52, 90, 26, kIdRefresh);
    mk(L"BUTTON", addMode ? L"Add" : L"Share", BS_DEFPUSHBUTTON,
        kW - 24 - 180, kH - 52, 86, 26, kIdOk);
    mk(L"BUTTON", L"Cancel", 0, kW - 24 - 88, kH - 52, 86, 26, kIdCancel);

    Repopulate(st);
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);

    MSG msg;
    BOOL got = TRUE;
    while (!st.done && (got = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (got == -1) break;
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // WM_QUIT không phải của hộp thoại này — nó dành cho vòng bơm NGOÀI (thread
    // quản lý phiên kết thúc trong lúc hộp thoại đang mở). Trả lại, không thì
    // vòng ngoài chặn ở GetMessage vĩnh viễn và join() thread đó bị treo.
    if (got == 0) PostQuitMessage(0);

    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    DestroyWindow(dlg);

    outSources = std::move(st.result);
    if (outAllowInput) *outAllowInput = st.allowInput;
    return !outSources.empty();
}

} // namespace

bool ShowScreenPickerDialog(HWND owner, std::vector<AgentSource>& outSources,
    bool& outAllowInput) {
    return RunPicker(owner, false, outSources, &outAllowInput);
}

bool ShowScreenPickerAddDialog(HWND owner, std::vector<AgentSource>& outSources) {
    return RunPicker(owner, true, outSources, nullptr);
}
