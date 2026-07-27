// =============================================================================
// MainMenuWindow.cpp — cài đặt màn hình chính bằng Win32 thuần.
//
// VÌ SAO WIN32 THUẦN, KHÔNG DÙNG FILE .rc HAY FRAMEWORK GIAO DIỆN
//   Cửa sổ và control tạo bằng CreateWindowExW trong mã. Đổi lại việc tự tính toạ
//   độ, ta được: không phụ thuộc thư viện ngoài, không bước biên dịch tài nguyên,
//   exe chạy độc lập. Với giao diện chỉ vài ô nhập và ít nút, đó là đánh đổi đúng.
//
// HAI HỘP: HOST MODE và CLIENT MODE
//     • "Host mode"   — chia sẻ máy này: địa chỉ IP (kèm Copy), FPS/Bitrate, nút Share.
//     • "Client mode" — kết nối máy khác: ô IP, nút Connect.
//   KHÔNG có ô Port và KHÔNG có ô "View only" (bỏ 2026-07-27): cổng luôn là 47777
//   và chuột/bàn phím luôn được chia sẻ, nên cả hai chỉ còn là chữ chứ không phải
//   lựa chọn. Xem kDeskhubPort trong ../cpp/net/UdpSocket.h.
//
// ĐƯỜNG RẼ HAI VAI — cả hai đều CHẶN, cửa sổ chính ẩn trong lúc phiên chạy
//   Share   → RunAgent() thẳng, chia sẻ HẾT màn hình (SessionWindow quản lý phiên)
//   Connect → QuerySources → SourcePickerDialog → RunViewer() (C API, Viewer.h)
//
// LIÊN QUAN: MainMenuWindow.h, SourcePickerDialog.h, capture/DisplayFinder.h,
//            Viewer.h, AgentLoop.h, ElevatedShare.h
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "MainMenuWindow.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "AgentLoop.h"
#include "ElevatedShare.h"
#include "SessionWindow.h"
#include "SourcePickerDialog.h"
#include "Viewer.h"
#include "capture/DisplayFinder.h"
#include "net/Firewall.h"
#include "net/NetInfo.h"
#include "net/SourceQuery.h"
#include "net/UdpSocket.h"
#include "deskhub/protocol/Wire.h" // kMaxSources

namespace {

constexpr wchar_t kWndClass[] = L"DeskhubMainMenu";

constexpr int kIdEditFps = 199;
constexpr int kIdEditBitrate = 198;
constexpr int kIdShare = 201;
constexpr int kIdEditAddr = 202;
constexpr int kIdConnect = 203;
constexpr int kIdExit = 205;
// Dải id cho các nút Copy: một dòng IP một nút, id = kIdCopyBase + chỉ số dòng.
constexpr int kIdCopyBase = 300;

constexpr uint32_t kDefaultFps = 60;
constexpr uint32_t kDefaultBitrateMbps = 20;

std::wstring Trim(std::wstring s) {
    while (!s.empty() &&
           (s.back() == L' ' || s.back() == L'\r' || s.back() == L'\n' || s.back() == L'\t'))
        s.pop_back();
    const size_t b = s.find_first_not_of(L" \t");
    return b == std::wstring::npos ? std::wstring() : s.substr(b);
}

uint32_t GetEditUint(HWND edit, uint32_t fallback) {
    wchar_t buf[16] = {};
    GetWindowTextW(edit, buf, 16);
    const int v = _wtoi(buf);
    return v > 0 ? (uint32_t)v : fallback;
}

// Đặt một chuỗi Unicode lên clipboard. Nếu SetClipboardData thành công thì hệ
// thống tiếp quản khối nhớ; ngược lại phải tự GlobalFree.
void CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (text.empty() || !OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* p = GlobalLock(h)) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(h);
            if (SetClipboardData(CF_UNICODETEXT, h)) h = nullptr; // clipboard sở hữu
        }
        if (h) GlobalFree(h);
    }
    CloseClipboard();
}

struct MenuState {
    HWND hwnd = nullptr;
    HWND editFps = nullptr;
    HWND editBitrate = nullptr;
    HWND editAddr = nullptr;
    std::vector<std::wstring> copyIps; // IP mỗi dòng, song song với các nút Copy
    bool quit = false;
};

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

void DoShare(MenuState& st) {
    // KHÔNG có bước chọn nguồn (bỏ 2026-07-27): bấm Share là chia sẻ HẾT màn hình
    // đang gắn. Danh sách chốt tại đây và không đổi trong suốt phiên — cắm thêm màn
    // hình giữa chừng thì phải dừng rồi share lại.
    std::vector<AgentSource> sources;
    for (const auto& d : ListDisplays()) {
        if (sources.size() >= deskhub::kMaxSources) {
            // kMaxSources là trần của SOURCE_LIST trong MỘT datagram, không phải một
            // con số tuỳ ý — máy nhiều màn hình hơn thế thì cắt bớt và nói ra.
            wchar_t msg[192];
            swprintf(msg, 192,
                L"This machine has more than %zu displays. Only the first %zu will be shared.",
                deskhub::kMaxSources, deskhub::kMaxSources);
            MessageBoxW(st.hwnd, msg, L"Deskhub", MB_OK | MB_ICONWARNING);
            break;
        }
        sources.push_back(AgentSource{d.monitor, ToUtf8(d.name)});
    }
    if (sources.empty()) {
        MessageBoxW(st.hwnd, L"No display found to share.", L"Deskhub",
            MB_OK | MB_ICONWARNING);
        return;
    }

    AgentOptions ao;
    ao.fps = GetEditUint(st.editFps, kDefaultFps);
    ao.bitrateMbps = GetEditUint(st.editBitrate, kDefaultBitrateMbps);

    // HAI lý do cần quyền admin, gộp vào MỘT lần bung UAC lúc bấm Share:
    //   • điều khiển — UIPI: SendInput vào tiến trình elevated bị nuốt IM LẶNG.
    //     Luôn cần, vì chuột/bàn phím luôn được chia sẻ.
    //   • firewall   — thêm rule inbound đòi admin, chỉ cần khi rule chưa có.
    if (!IsProcessElevated()) {
        const bool needFirewall = !HostFirewallRulePresent();
        bool cancelled = false;
        if (RelaunchElevatedShare(sources, ao, cancelled)) {
            st.quit = true; // instance admin tiếp quản đúng phiên này
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
    // RunAgent nhận AgentControl; SessionWindow là bản Win32 của nó. Mở trước,
    // đóng sau khi phiên kết thúc.
    SessionWindow session;
    session.Start();
    RunAgent(sources, ao, session);
    session.Stop();
    ShowWindow(st.hwnd, SW_SHOW);
    SetForegroundWindow(st.hwnd);
}

void DoConnect(MenuState& st) {
    wchar_t buf[128] = {};
    GetWindowTextW(st.editAddr, buf, 128);
    const std::wstring waddr = Trim(buf);
    if (waddr.empty()) {
        MessageBoxW(st.hwnd, L"Enter the host machine's IP address first (e.g., 192.168.1.10).",
            L"Deskhub", MB_OK | MB_ICONWARNING);
        return;
    }
    // Địa chỉ IP chỉ gồm ASCII — thu hẹp từng ký tự là an toàn.
    std::string addr;
    addr.reserve(waddr.size());
    for (wchar_t c : waddr) addr.push_back(char(c));

    NetAddr server{};
    if (!ParseNetAddr(addr, server)) {
        // Nói rõ cả cổng, vì đường sai hay gặp nhất là người dùng gõ kèm ":port"
        // theo thói quen — ParseNetAddr từ chối chuỗi đó chứ không lờ đi.
        const std::wstring msg = L"Invalid address: \"" + waddr +
                                 L"\"\nEnter just the IP address (e.g., 192.168.1.10). "
                                 L"Deskhub always uses UDP port 47777.";
        MessageBoxW(st.hwnd, msg.c_str(), L"Deskhub", MB_OK | MB_ICONERROR);
        return;
    }

    // Hỏi host đang chia sẻ những gì rồi cho chọn. Host bản cũ (hoặc sai IP /
    // firewall chặn) không trả lời -> cứ thử nguồn 0, phiên sẽ báo lỗi kết nối
    // cụ thể hơn nhiều so với một hộp thoại "không thấy host".
    std::vector<deskhub::SourceInfo> available;
    std::vector<deskhub::SourceInfo> picked;
    if (QuerySources(server, available) && !available.empty()) {
        if (!ShowSourcePickerDialog(st.hwnd, available, picked)) return;
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
        case WM_CLOSE:
            if (st) st->quit = true;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

int RunMainMenuWindow() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWndClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    MenuState st;

    // Địa chỉ IPv4 của máy này — biết trước số dòng để tính chiều cao hộp host.
    const auto addrs = ListLocalIPv4();
    const int nRows = addrs.empty() ? 1 : (int)addrs.size();

    // --- Bố cục: hai group box xếp dọc, dưới cùng là nút Exit ---
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
    const int clientH = 100; // thấp hơn bản trước 18px: checkbox View only đã bỏ
    const int exitY = clientTop + clientH + 14;
    const int kH = exitY + 44;

    const DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT wr{0, 0, kW, kH};
    AdjustWindowRect(&wr, style, FALSE);
    HWND hwnd = CreateWindowExW(0, kWndClass,
        L"Deskhub - stream & remotely control an application", style, CW_USEDEFAULT,
        CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, wc.hInstance,
        nullptr);
    if (!hwnd) return 1;

    st.hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&st);

    const HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD s, int cx, int cy, int cw,
                  int ch, int id) {
        HWND c = CreateWindowExW(0, cls, text, s | WS_CHILD | WS_VISIBLE, cx, cy, cw, ch, hwnd,
            (HMENU)(INT_PTR)id, wc.hInstance, nullptr);
        if (c) SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        return c;
    };

    // Group box tạo TRƯỚC control con để con vẽ đè lên khung (z-order theo thứ tự tạo).
    mk(L"BUTTON", L"Host mode - share an application on THIS machine", BS_GROUPBOX, gx, hostTop,
        gw, hostH, 0);

    mk(L"STATIC", L"Others connect to you using one of these IP addresses:", SS_LEFT, ix,
        hostTop + 22, iw, 16, 0);

    constexpr int kCopyW = 60, kCopyH = 20;
    const int copyX = gx + gw - 14 - kCopyW;
    if (addrs.empty()) {
        mk(L"STATIC", L"(no network address found)", SS_LEFT, ix, hostTop + 44, iw, 18, 0);
    } else {
        st.copyIps.reserve(addrs.size());
        int i = 0;
        for (const auto& a : addrs) {
            const int rowY = hostTop + 44 + i * rowH;
            // Giá trị Copy là IP TRẦN — đúng thứ phải gõ vào ô địa chỉ phía kia.
            std::wstring addr(a.ip.begin(), a.ip.end());
            wchar_t line[192];
            swprintf(line, 192, L"%-20ls %ls", a.name.c_str(), addr.c_str());
            mk(L"STATIC", line, SS_LEFT | SS_ENDELLIPSIS, ix, rowY + 2, copyX - 8 - ix, 18, 0);
            mk(L"BUTTON", L"Copy", BS_PUSHBUTTON, copyX, rowY, kCopyW, kCopyH, kIdCopyBase + i);
            st.copyIps.push_back(std::move(addr));
            ++i;
        }
    }

    // Không có ô Port: cổng là hằng số 47777 (net/UdpSocket.h). Chỉ nói ra cho người
    // dùng biết con số đó, vì firewall doanh nghiệp có thể cần mở tay.
    const int sy = hostTop + settingsRel;
    mk(L"STATIC", L"UDP port 47777", SS_LEFT, ix, sy + 3, 100, 18, 0);
    mk(L"STATIC", L"FPS", SS_LEFT, ix + 116, sy + 3, 30, 18, 0);
    st.editFps = mk(L"EDIT", L"60", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, ix + 148, sy, 48, 24,
        kIdEditFps);
    mk(L"STATIC", L"Bitrate (Mbps)", SS_LEFT, ix + 212, sy + 3, 90, 18, 0);
    st.editBitrate = mk(L"EDIT", L"20", WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, ix + 304, sy, 48,
        24, kIdEditBitrate);

    mk(L"BUTTON", L"Share...  (pick the display to share)", BS_PUSHBUTTON, ix,
        hostTop + shareRel, iw, 32, kIdShare);

    // --- Hộp client ---
    mk(L"BUTTON", L"Client mode - connect to ANOTHER machine", BS_GROUPBOX, gx, clientTop, gw,
        clientH, 0);

    mk(L"STATIC", L"Host machine IP address:", SS_LEFT, ix, clientTop + 24, iw, 16, 0);
    st.editAddr =
        mk(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, ix, clientTop + 44, iw - 110, 26,
            kIdEditAddr);
    mk(L"BUTTON", L"Connect", BS_DEFPUSHBUTTON, ix + iw - 100, clientTop + 44, 100, 26,
        kIdConnect);

    mk(L"BUTTON", L"Exit", BS_PUSHBUTTON, gx, exitY, 100, 28, kIdExit);

    ShowWindow(hwnd, SW_SHOW);

    MSG msg;
    BOOL got;
    while (!st.quit && (got = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (got == -1) break;
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    DestroyWindow(hwnd);
    return 0;
}
