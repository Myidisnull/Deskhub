#pragma once
// =============================================================================
// LogFile.h — chỗ log rơi xuống đĩa, MỘT đường dẫn cho mọi OS: ~/.deskhub/
//
// NHIỆM VỤ
//   Tạo (nếu chưa có) thư mục log chuẩn và mở sẵn file log của tiến trình này.
//   Nhờ vậy câu hướng dẫn "gửi tôi file mới nhất trong ~/.deskhub" đúng nguyên văn
//   trên Windows, macOS lẫn Ubuntu — trước đây mỗi nền một chỗ, và trên hai nền
//   Unix thì log chỉ tồn tại nếu người dùng nhớ chạy app từ terminal.
//
// VÌ SAO ~/.deskhub CHỨ KHÔNG PHẢI CẠNH EXE (chỗ cũ của bản Windows)
//   Thư mục chứa app thường CHỈ ĐỌC ở đúng những lần cài đặt chuẩn nhất: Program
//   Files trên Windows, /Applications trên macOS (ghi vào trong .app còn phá chữ
//   ký), /usr/bin trên Ubuntu. Ghi cạnh exe nghĩa là máy nào cài tử tế thì máy đó
//   KHÔNG có log — ngược hoàn toàn với mục đích. Thư mục nhà luôn ghi được và luôn
//   thuộc về đúng người đang gặp lỗi.
//
// MỖI TIẾN TRÌNH MỘT FILE + MỘT SYMLINK CỐ ĐỊNH
//   Tên: deskhub-<ngày>-<giờ>-<pid>.log (giờ ĐỊA PHƯƠNG — người dùng đối chiếu với
//   "lúc nãy khoảng 8 rưỡi nó giật", và họ đọc đồng hồ máy mình). Có pid vì hai
//   tiến trình chạy song song là chuyện bình thường (Windows: instance thường +
//   instance admin của ElevatedShare; Unix: mở hai app để thử host↔client).
//   Kèm theo, trên Unix có symlink deskhub-latest.log trỏ vào file của lần chạy này
//   để `tail -f ~/.deskhub/deskhub-latest.log` luôn dùng được mà không phải đi tìm
//   tên. Không có symlink trên Windows: tạo symlink ở đó cần quyền riêng.
//
// ⚠ GHI LOG KHÔNG ĐƯỢC CHẶN LUỒNG NÓNG
//   Dòng [DIAG] in dày trên vòng recv/encode. File log vì thế dùng BUFFER LỚN
//   (_IOFBF): mỗi lần log chỉ là memcpy vào RAM. Một thread nền xả buffer ra đĩa
//   mỗi ~500ms, nên syscall ghi đĩa nằm trên thread phụ chứ không trên luồng nóng —
//   cùng lập luận và cùng con số với client/windows/cpp/DiagLog.cpp. Mất mát tối đa
//   khi crash ~ một chu kỳ xả.
//
//   Thread xả là detached và KHÔNG có đường tắt: file log sống trọn đời tiến trình.
//   Cùng đánh đổi đã ghi ở DiagLog.cpp — không có Stop() nghĩa là không có cửa sổ
//   nào để một thread khác đóng file dưới chân thread xả.
//
// PHẠM VI: DESKTOP
//   Android đi logcat và iOS đi Xcode console — cả hai là kênh chuẩn của nền tảng,
//   và ~ trên hai OS đó nằm trong sandbox nên file ghi ra không ai lấy được. Hai
//   bản Log.h ấy cố ý KHÔNG dùng header này.
//
// LIÊN QUAN: client/macos/app/cpp/Log.h, client/linux/cpp/Log.h (nơi gọi LogEmit),
//            client/windows/cpp/DiagLog.cpp (nơi dùng LogDirW), docs/09-diagnostics.md
// =============================================================================
#include <cstdio>
#include <ctime>
#include <string>

#ifdef _WIN32
// Guard từng macro: các header khác (Clock.h, Random.h, phần lớn client/windows/cpp)
// cũng đặt hai macro này, và định nghĩa lại với giá trị khác là C4005 — với /WX thì
// gãy build. Cả hai chỉ cần được đặt MỘT LẦN trước windows.h.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <thread>
#endif

namespace deskhubp {

// Tên file log của tiến trình này: deskhub-<yyyymmdd>-<hhmmss>-<pid>.log
// Cả ba nền dùng chung đúng quy ước này — docs/09 ghi nó thành tài liệu, và người
// đọc log dựa vào nó để sắp thứ tự các lần chạy.
inline std::string LogFileName() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
    const unsigned long pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
    localtime_r(&now, &tm);
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    char name[80];
    std::snprintf(name, sizeof(name), "deskhub-%04d%02d%02d-%02d%02d%02d-%lu.log",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, pid);
    return std::string(name);
}

// Giờ ĐỊA PHƯƠNG dạng "HH:MM:SS", để đóng dấu các dòng trạng thái mỗi giây.
//
// ⚠ VÌ SAO CẦN — CHẨN ĐOÁN LUÔN PHẢI ĐỌC HAI LOG CÙNG LÚC.
//   Mọi sự cố trong dự án này đều nằm giữa hai máy, nên cách làm việc thật là mở
//   log host và log client cạnh nhau rồi tìm cùng một giây trên cả hai. Trước
//   30/07/2026 các dòng mỗi-giây KHÔNG có mốc giờ nào: hai file chỉ có giờ bắt đầu
//   ở tên file, mà hai tiến trình gần như không bao giờ khởi động cùng lúc — và chỉ
//   cần một bên khởi động lại giữa chừng là mọi phép căn theo "dòng thứ N" sai hết.
//   Đã mất vài vòng chẩn đoán vì đúng chuyện đó.
//
//   Chỉ đóng dấu các dòng MỖI GIÂY chứ không phải mọi dòng: nhánh Windows đổi hướng
//   thẳng stdout bằng freopen (DiagLog.h) nên không có chỗ chen tiền tố vào từng
//   dòng, còn các dòng mỗi-giây thì ta gọi tay nên đặt được ở cả ba nền — và chúng
//   mới là thứ cần căn.
//
//   Giờ ĐỊA PHƯƠNG, khớp với quy ước của LogFileName() ở trên. Hai máy lệch múi giờ
//   thì vẫn phải quy đổi bằng tay, nhưng ca thường gặp là hai máy cạnh nhau.
inline std::string LocalTimeHms() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf);
}

#ifdef _WIN32

// ---------------------------------------------------------------------------
// Nhánh Windows. Đường dẫn giữ nguyên WIDE tới tận nơi dùng: tên người dùng có dấu
// (rất hay gặp ở đây) mà đi qua std::string theo code page hệ thống là hỏng đường
// dẫn. LogDir() dạng UTF-8 vẫn có để API đồng tên giữa các nền, nhưng phía Windows
// hãy dùng LogDirW().
// ---------------------------------------------------------------------------

// Thư mục log, đã tạo nếu chưa có. Rỗng = không xác định được thư mục nhà hoặc
// không tạo được thư mục; người gọi chạy tiếp không có log, không phải lỗi tử vong.
inline std::wstring LogDirW() {
    wchar_t buf[MAX_PATH] = {};
    // USERPROFILE là %HOME% của Windows: C:\Users\<tên>. Có trong mọi phiên đăng
    // nhập tương tác, kể cả tiến trình đã bung UAC (nó kế thừa profile của người
    // bấm "Yes"), nên instance admin của ElevatedShare ghi log cùng chỗ.
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();

    std::wstring dir = std::wstring(buf, n) + L"\\.deskhub";
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return std::wstring();
    return dir;
}

inline std::string LogDir() {
    const std::wstring w = LogDirW();
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

// KHÔNG có LogEmit trên Windows: ở đó log đi bằng printf thẳng và
// DiagLog::StartProcessLog đã đổi hướng stdout/stderr vào chính file này. Thêm một
// đường ghi thứ hai vào cùng file là mỗi dòng ra hai lần.

#else

// ---------------------------------------------------------------------------
// Nhánh POSIX (macOS + Ubuntu).
// ---------------------------------------------------------------------------

// Thư mục log, đã tạo nếu chưa có, KHÔNG có dấu / ở cuối. Rỗng = không xác định
// được thư mục nhà hoặc không tạo được thư mục (đĩa đầy, quyền lạ) — người gọi
// chạy tiếp không có file log, đây không phải lỗi tử vong.
inline std::string LogDir() {
    const char* home = std::getenv("HOME");
    // getpwuid là đường lùi cho tiến trình khởi động không qua shell (launchd trên
    // macOS, .desktop trên Ubuntu) — vài môi trường trong số đó không đặt HOME.
    if (!home || !*home) {
        const struct passwd* pw = getpwuid(getuid());
        home = (pw && pw->pw_dir) ? pw->pw_dir : nullptr;
    }
    if (!home || !*home) return std::string();

    std::string dir = std::string(home) + "/.deskhub";
    // 0700: log chứa tên máy, địa chỉ IP, tên màn hình đang chia sẻ. Không có lý do
    // gì để người dùng khác trên cùng máy đọc được.
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) return std::string();
    return dir;
}

// Đường dẫn file log của tiến trình này; rỗng cho tới khi LogHandle() mở được file.
// UI dùng nó để chỉ cho người dùng chỗ lấy log.
inline std::string& LogPathRef() {
    static std::string path;
    return path;
}

// FILE* của log, mở LƯỜI ở lần log đầu tiên. Lười chứ không phải một hàm Start()
// gọi từ main: hai vai (client/agent) có hai điểm vào khác nhau trên cả ba UI, và
// chỉ một chỗ quên gọi là mất log đúng lúc cần nhất. Magic static của C++ bảo đảm
// khởi tạo đúng một lần dù thread nào chạm trước.
// nullptr = không mở được; từ đó về sau log chỉ còn ra stderr.
inline std::FILE* LogHandle() {
    static std::FILE* file = []() -> std::FILE* {
        const std::string dir = LogDir();
        if (dir.empty()) return nullptr;

        const std::string path = dir + "/" + LogFileName();
        std::FILE* f = std::fopen(path.c_str(), "w");
        if (!f) return nullptr;
        LogPathRef() = path;

        // Buffer lớn: luồng nóng chỉ memcpy, không chạm đĩa (xem ⚠ ở đầu file).
        std::setvbuf(f, nullptr, _IOFBF, 256 * 1024);

        // Symlink "mới nhất" để tail -f khỏi phải tra tên. Xoá cái cũ trước vì
        // symlink() từ chối ghi đè. Hỏng thì thôi — tiện ích, không phải log.
        const std::string latest = dir + "/deskhub-latest.log";
        ::unlink(latest.c_str());
        ::symlink(path.c_str(), latest.c_str());

        // Thread xả buffer, KHÔNG trên luồng nóng. Detached, chạy tới lúc tiến
        // trình thoát. Bắt FILE* theo GIÁ TRỊ: nó không bao giờ bị đóng lại.
        std::thread([f] {
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                std::fflush(f);
            }
        }).detach();

        return f;
    }();
    return file;
}

// Ghi MỘT dòng log ra cả stderr lẫn file.
//
// VÌ SAO CẢ HAI CHỨ KHÔNG ĐỔI HƯỚNG NHƯ WINDOWS
//   Bản Windows freopen stdout vì app ở đó không có console để mà xem. Trên Unix
//   thì ngược lại: chạy từ terminal hoặc từ Xcode là đường debug hằng ngày, và
//   cướp mất stderr sẽ làm nó câm. Ở đây mọi log đều đi qua đúng hàm này (khác
//   Windows với hàng trăm printf rải rác), nên viết hai chỗ là chuyện vặt.
//
// Dòng được dựng TRỌN VẸN rồi mới ghi bằng MỘT lời gọi: fprintf khoá FILE* theo
// từng lời gọi, nên một-lời-gọi-một-dòng là thứ giữ cho log của các thread không
// cài răng lược vào nhau. Dòng dài hơn bộ đệm bị cắt — mọi dòng trong dự án đều
// ngắn hơn nhiều, và cắt vẫn hơn cấp phát động trên luồng nóng.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
inline void LogEmit(const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n < 0) return;

    std::fprintf(stderr, "[Deskhub] %s\n", msg);
    if (std::FILE* f = LogHandle()) std::fprintf(f, "[Deskhub] %s\n", msg);
}

#endif // _WIN32

} // namespace deskhubp
