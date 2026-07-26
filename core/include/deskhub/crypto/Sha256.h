#pragma once
// =============================================================================
// Sha256.h — SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256 và so sánh hằng thời gian.
//
// NHIỆM VỤ
//   Nền móng số học cho tầng xác thực (deskhub/auth/). Ba nguyên hàm, không hơn:
//     Sha256      — hàm băm (FIPS 180-4)
//     HmacSha256  — MAC có khoá (RFC 2104)
//     Pbkdf2Sha256— dẫn xuất khoá từ mật khẩu, làm chậm có chủ ý (RFC 8018 §5.2)
//   Cộng thêm ConstantTimeEqual, thứ bắt buộc phải có khi so sánh bí mật.
//
// VÌ SAO TỰ CÀI CHỨ KHÔNG LINK THƯ VIỆN CRYPTO
//   core/ được biên dịch bởi MSVC, AppleClang, Android NDK và Xcode bằng CÙNG một
//   mã nguồn, và nguyên tắc bất di bất dịch của nó là KHÔNG include header hệ điều
//   hành nào (xem core/CMakeLists.txt). Mỗi nền tảng có một thư viện crypto khác
//   nhau — BCrypt trên Windows, CommonCrypto trên Apple, và Android NDK thì không
//   có sẵn cái nào — nên dùng chúng sẽ đẻ ra bốn nhánh #ifdef ngay giữa lõi, đúng
//   thứ mà core/ sinh ra để tránh.
//
//   SHA-256 là thuật toán CỐ ĐỊNH, đã chuẩn hoá, ~150 dòng, và kiểm chứng được
//   bằng vector chuẩn của NIST/RFC (xem core/tests/crypto/CryptoTests.cpp). Đây là
//   loại mã "viết một lần, đúng mãi mãi" — khác hẳn với việc tự cài một giao thức.
//
//   PHẦN KHÔNG tự cài: số ngẫu nhiên. Không có nguồn entropy nào không phụ thuộc
//   OS, và tự chế PRNG cho mục đích mã hoá là lỗi kinh điển. Nó nằm ở
//   platform/include/deskhubp/Random.h.
//
// PHẠM VI BẢO VỆ (đọc kỹ trước khi tin vào lớp này)
//   Mục tiêu duy nhất: chứng minh "bên kia biết mật khẩu" mà KHÔNG đưa mật khẩu lên
//   dây. Nó KHÔNG mã hoá luồng — video, input và clipboard vẫn đi trần trên UDP,
//   đúng như PRIVACY.md đã công bố. Kẻ nghe được đường truyền vẫn đọc được nội dung
//   phiên; cái nó không làm được là ĐOẠT lấy phiên hay tự mở một phiên mới.
//   Mã hoá luồng (DTLS/AEAD) vẫn là GĐ6 của docs/05-roadmap.md.
//
// LIÊN QUAN: deskhub/auth/PasswordAuth.h (người dùng chính), deskhubp/Random.h
// =============================================================================
#include <cstddef>
#include <cstdint>
#include <span>

namespace deskhub {

inline constexpr size_t kSha256Bytes = 32;
inline constexpr size_t kSha256BlockBytes = 64;

// Băm `data`, ghi 32 byte vào `out`. `out` phải rộng đúng kSha256Bytes.
void Sha256(std::span<const uint8_t> data, std::span<uint8_t> out);

// Dạng nạp dần — dùng khi phải băm nhiều mảnh rời mà không muốn nối chúng lại
// trong bộ nhớ. HmacSha256 bên dưới xây trên chính lớp này.
class Sha256Ctx {
public:
    Sha256Ctx() {
        Reset();
    }
    void Reset();
    void Update(std::span<const uint8_t> data);
    // Chốt và ghi 32 byte vào `out`. Sau Final, phải Reset trước khi dùng lại.
    void Final(std::span<uint8_t> out);

private:
    void Compress(const uint8_t* block);

    uint32_t h_[8];
    uint8_t buf_[kSha256BlockBytes];
    size_t bufLen_;
    uint64_t totalBits_;
};

// HMAC-SHA256(key, data) → 32 byte vào `out`. Khoá dài bất kỳ (RFC 2104: khoá dài
// hơn một block thì băm trước, ngắn hơn thì đệm 0).
void HmacSha256(std::span<const uint8_t> key, std::span<const uint8_t> data,
    std::span<uint8_t> out);

// PBKDF2-HMAC-SHA256 với dkLen = 32 (đúng một block đầu ra, nên bỏ được vòng lặp
// ghép khối của RFC 8018). `iterations` phải ≥ 1.
//
// Vì sao lặp nhiều lần: mục đích DUY NHẤT là làm chậm việc dò mật khẩu ngoại tuyến
// nếu kho khoá của host bị đọc. Một phép băm đơn chạy hàng tỉ lần mỗi giây trên
// GPU; ép mỗi lần thử tốn ~100 ms khiến việc dò từ điển trở nên vô nghĩa về mặt
// kinh tế. Chi phí phía người dùng là 100 ms MỘT LẦN lúc kết nối — không nằm trên
// đường nóng của phiên.
void Pbkdf2Sha256(std::span<const uint8_t> password, std::span<const uint8_t> salt,
    uint32_t iterations, std::span<uint8_t> out);

// So sánh hai khối byte trong thời gian KHÔNG phụ thuộc nội dung của chúng.
//
// Bắt buộc dùng cái này thay cho memcmp khi một trong hai bên là bí mật. memcmp
// thoát ngay tại byte lệch đầu tiên, nên thời gian chạy của nó rò rỉ số byte đầu
// đã đúng — lặp lại phép đo đủ nhiều lần là dò ra được cả chuỗi, từng byte một.
// Hàm này luôn duyệt hết và gộp khác biệt bằng OR.
bool ConstantTimeEqual(std::span<const uint8_t> a, std::span<const uint8_t> b);

} // namespace deskhub
