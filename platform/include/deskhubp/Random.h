#pragma once
// =============================================================================
// Random.h — số ngẫu nhiên dùng cho MÃ HOÁ, một tên cho mọi OS.
//
// NHIỆM VỤ
//   RandomBytes(): lấp một bộ đệm bằng byte không đoán được. Ba nơi tiêu thụ:
//     1. nonce của AUTH_CHALLENGE — chống phát lại (replay) một lời đáp cũ;
//     2. salt của mật khẩu — chống bảng tra dựng sẵn (rainbow table);
//     3. sessionId và deviceToken — hai bí mật mà đoán trúng là đoạt được phiên.
//
// VỊ TRÍ TRONG KIẾN TRÚC
//   core/ (thuần C++20, cấm đụng OS)  ←── byte ──  **platform/Random.h**  ──→ OS API
//   Cùng ranh giới với Clock.h và cùng một lý do: core/ phải biên dịch được cho
//   Windows, Android NDK, iOS, Ubuntu bằng cùng một mã nguồn.
//
//   Đây là thứ DUY NHẤT của tầng xác thực không nằm trong core/. SHA-256, HMAC và
//   PBKDF2 là thuật toán thuần tuý nên core/ tự cài được (deskhub/crypto/Sha256.h);
//   entropy thì không — nó phải xin từ nhân hệ điều hành.
//
// VÌ SAO KHÔNG DÙNG std::mt19937 / rand() / đồng hồ
//   Vì chúng ĐOÁN ĐƯỢC. mt19937 lộ trọn trạng thái nội bộ sau 624 lần xuất; rand()
//   còn tệ hơn; và trộn đồng hồ vào — đúng cách HostSession::HandlePacket đang sinh
//   sessionId hôm nay — cho ra một con số mà kẻ tấn công thu hẹp được xuống vài
//   nghìn khả năng chỉ bằng cách biết máy vừa khởi động lúc nào.
//   Mọi hàm dưới đây đều là CSPRNG của nhân, đã gieo từ entropy phần cứng.
//
// KHÔNG CÓ ĐƯỜNG THẤT BẠI IM LẶNG
//   Trả về bool. Người gọi PHẢI kiểm tra: một bộ đệm nonce toàn số 0 vì lời gọi
//   thất bại trông y hệt một nonce hợp lệ, và nó sẽ vô hiệu hoá toàn bộ lớp bảo vệ
//   mà không có triệu chứng nào. Đó là lý do hàm này không trả về void.
//
// LIÊN QUAN: deskhub/crypto/Sha256.h, deskhub/auth/PasswordAuth.h,
//            platform/include/deskhubp/Clock.h (cùng ranh giới, cùng lý do)
// =============================================================================
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Nhánh Windows: BCryptGenRandom với BCRYPT_USE_SYSTEM_PREFERRED_RNG.
//
// Đây là API được Microsoft khuyến nghị hiện nay; CryptGenRandom (CryptoAPI cũ) đã
// bị đánh dấu lỗi thời. Cờ USE_SYSTEM_PREFERRED_RNG cho phép truyền nullptr làm
// handle thuật toán nên không phải mở/đóng provider.
//
// #pragma comment giữ cho header này TỰ ĐỦ: không CMakeLists nào của các client
// phải thêm bcrypt vào target_link_libraries. Nếu sau này chuyển platform/ thành
// STATIC (xem platform/CMakeLists.txt) thì đổi sang khai báo link tường minh.
// ---------------------------------------------------------------------------
#ifdef _WIN32
// Guard từng macro: các header khác (Clock.h, và phần lớn file trong
// client/windows/cpp) cũng đặt hai macro này. Định nghĩa lại một macro đã có giá
// trị khác là C4005 trên MSVC — với /WX thì gãy build, mà cả hai chỉ cần được đặt
// MỘT LẦN trước windows.h là đủ.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <bcrypt.h>
#pragma comment(lib, "bcrypt")

inline bool RandomBytes(void* out, size_t n) {
    if (!out || n == 0) return false;
    const NTSTATUS st = BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), static_cast<ULONG>(n),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return st >= 0; // NT_SUCCESS
}

// ---------------------------------------------------------------------------
// Nhánh Apple (macOS, iOS): arc4random_buf.
//
// Chọn nó thay vì getentropy vì arc4random_buf KHÔNG CÓ đường thất bại — nó không
// trả mã lỗi và không giới hạn 256 byte mỗi lần gọi như getentropy. Có sẵn từ rất
// lâu trên mọi phiên bản Apple ta hỗ trợ.
// ---------------------------------------------------------------------------
#elif defined(__APPLE__)
#include <cstdlib>

inline bool RandomBytes(void* out, size_t n) {
    if (!out || n == 0) return false;
    arc4random_buf(out, n);
    return true;
}

// ---------------------------------------------------------------------------
// Nhánh Linux / Android: getrandom(2), lùi về /dev/urandom nếu không có.
//
// getrandom cần glibc 2.25+ / Android API 28+. Ta vẫn giữ đường lùi đọc
// /dev/urandom vì NDK hạ được minSdk xuống dưới 28, và vì trong container tối giản
// syscall này đôi khi bị seccomp chặn. Cả hai nguồn là cùng một CSPRNG của nhân.
//
// getrandom trả về tối đa 256 byte mỗi lần với bộ đệm nhỏ, và có thể bị ngắt bởi
// tín hiệu (EINTR) — nên phải gọi trong vòng lặp, không phải một phát ăn ngay.
// ---------------------------------------------------------------------------
#else
#include <cerrno>
#include <cstdio>
#include <unistd.h>

#if defined(__linux__)
#include <sys/syscall.h>
#endif

inline bool RandomBytes(void* out, size_t n) {
    if (!out || n == 0) return false;
    auto* p = static_cast<unsigned char*>(out);
    size_t got = 0;

#if defined(__linux__) && defined(SYS_getrandom)
    while (got < n) {
        const long r = syscall(SYS_getrandom, p + got, n - got, 0);
        if (r > 0) {
            got += static_cast<size_t>(r);
            continue;
        }
        if (r < 0 && errno == EINTR) continue; // bị tín hiệu ngắt — thử lại
        break;                                 // ENOSYS/EPERM… → rơi xuống /dev/urandom
    }
    if (got == n) return true;
#endif

    std::FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) return false;
    const size_t rd = std::fread(p + got, 1, n - got, f);
    std::fclose(f);
    return got + rd == n;
}

#endif

// Tiện ích: sinh một u32 không đoán được. Trả 0 khi thất bại — người gọi phải coi
// 0 là lỗi, không phải một giá trị hợp lệ (mọi chỗ dùng nó trong dự án đều đã quy
// ước sessionId/probeId khác 0).
inline uint32_t RandomU32() {
    uint32_t v = 0;
    if (!RandomBytes(&v, sizeof(v))) return 0;
    return v;
}
