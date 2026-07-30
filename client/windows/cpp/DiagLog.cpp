// =============================================================================
// DiagLog.cpp — cài đặt việc đổi hướng toàn bộ log của tiến trình sang file.
//
// VÌ SAO FREOPEN, VÌ SAO MỘT FILE MỖI TIẾN TRÌNH: xem DiagLog.h.
//
// ⚠ KHÔNG ĐỂ GHI LOG CHẶN LUỒNG NÓNG
//   Log LUÔN bật và [DIAG] in dày trên vòng recv/encode. Nếu để stdout unbuffered
//   thì mỗi printf là một lời gọi ghi đĩa NGAY trên luồng đó — gặp đĩa chậm hay AV
//   quét file là recv ngừng nghe, buffer UDP tràn, mất gói THẬT (đúng cái recv_stall
//   mà log định bắt). Nên:
//     1. stdout dùng BUFFER LỚN (_IOFBF): printf trên luồng nóng chỉ là memcpy vào
//        RAM, không đụng đĩa.
//     2. Một THREAD FLUSH NỀN xả buffer ra đĩa mỗi ~500 ms. Việc ghi đĩa (syscall)
//        nằm trên thread phụ này, không trên luồng chính; và nhờ xả đều nên buffer
//        gần như không bao giờ tự đầy giữa hai lần xả (tránh nốt lần flush hiếm hoi
//        rơi vào luồng nóng). Mất mát tối đa khi crash ~ một chu kỳ xả.
//     3. stderr để unbuffered: lỗi hiếm và cần chạm đĩa ngay, không lo chặn.
//
// KHÔNG CÓ ĐƯỜNG TRẢ LẠI (không Stop/restore): app không còn console để trả stdout
// về, file log sống trọn đời tiến trình — thoát bình thường thì CRT tự flush stdio,
// hệ điều hành đóng file. Thread flush là daemon detached, chết theo tiến trình.
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "DiagLog.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <io.h>
#include <thread>

#include "deskhubp/LogFile.h"

namespace {

// Buffer của stdout: đủ lớn để vài giây log dồn dập không làm nó tự đầy giữa hai
// lần flush nền. Sống trọn đời tiến trình (BSS) vì stdout tham chiếu tới nó.
char g_logBuf[256 * 1024];

} // namespace

bool StartProcessLog(std::wstring* outPath) {
    SYSTEMTIME t{};
    GetLocalTime(&t);

    // Tên file: quy ước dùng chung ba nền, giữ ở deskhubp::LogFileName() để không
    // có hai chỗ định nghĩa cùng một định dạng tên. Giờ ĐỊA PHƯƠNG chứ không phải
    // UTC (người dùng đối chiếu với đồng hồ máy mình) và có pid để tách file của
    // instance thường khỏi instance admin — lý do đầy đủ ở LogFile.h.
    // Tên toàn ASCII nên nới sang wchar_t bằng ép kiểu từng ký tự là đủ — ép TƯỜNG
    // MINH chứ không dùng constructor theo cặp iterator, thứ để MSVC tự thu hẹp
    // char→wchar_t và kêu C4244.
    const std::string nameUtf8 = deskhubp::LogFileName();
    std::wstring name;
    name.reserve(nameUtf8.size());
    for (char c : nameUtf8) name.push_back(static_cast<wchar_t>(c));

    // ~/.deskhub chứ không còn cạnh exe (đổi 2026-07-30): cùng đường dẫn với bản
    // macOS và Ubuntu, và ghi được kể cả khi exe nằm trong Program Files — chỗ mà
    // cách cũ luôn thất bại.
    const std::wstring dir = deskhubp::LogDirW();
    if (dir.empty()) return false;
    const std::wstring full = dir + L"\\" + name;

    if (!_wfreopen(full.c_str(), L"w", stdout)) return false;
    // Buffer lớn: hot path chỉ memcpy, không ghi đĩa từng dòng (xem đầu file).
    setvbuf(stdout, g_logBuf, _IOFBF, sizeof(g_logBuf));

    // stderr gộp chung file với stdout (một file để gửi), nhưng để unbuffered để
    // lỗi chạm đĩa ngay.
    _dup2(_fileno(stdout), _fileno(stderr));
    setvbuf(stderr, nullptr, _IONBF, 0);

    if (outPath) *outPath = full;

    // In ĐƯỜNG DẪN ĐẦY ĐỦ, không chỉ tên file: log không còn nằm cạnh exe nên dòng
    // đầu tiên phải tự nói ra nó đang ở đâu.
    std::printf("[DiagLog] %ls started %04u-%02u-%02u %02u:%02u:%02u\n",
        full.c_str(), t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);

    // Thread xả buffer ra đĩa định kỳ, KHÔNG trên luồng nóng. Detached: chạy tới khi
    // tiến trình thoát.
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::fflush(stdout);
        }
    }).detach();

    return true;
}
