#pragma once
// =============================================================================
// PasswordAuth.h — phép toán của bắt tay challenge-response. Không trạng thái.
//
// NHIỆM VỤ
//   Trả lời đúng một câu: "bên kia có biết mật khẩu không?" — mà KHÔNG bao giờ để
//   mật khẩu đi lên dây. Phần có trạng thái (ai đang thử, sai mấy lần, thiết bị nào
//   được nhớ) nằm ở deskhub/auth/AuthGuard.h; ở đây chỉ có hàm thuần.
//
// GIAO THỨC (docs/04-protocol.md §9)
//
//   client                                                     host
//     │ HELLO (kèm deviceToken nếu đã từng được nhớ)             │
//     │────────────────────────────────────────────────────────► │
//     │                                     token khớp? → bỏ qua phần dưới
//     │ AUTH_CHALLENGE  salt(16) iters(u32) nonce(32)            │
//     │ ◄────────────────────────────────────────────────────────│
//     │ key   = PBKDF2(password, salt, iters)                    │
//     │ proof = HMAC(key, nonce ‖ clientId)                      │
//     │ AUTH_RESPONSE  proof(32)                                 │
//     │────────────────────────────────────────────────────────► │
//     │                      host tính lại proof bằng key đã lưu và so hằng thời gian
//     │ HELLO_ACK (sessionId, + deviceToken mới nếu được nhớ)    │
//     │ ◄────────────────────────────────────────────────────────│
//
// VÌ SAO CHALLENGE-RESPONSE CHỨ KHÔNG GỬI THẲNG MẬT KHẨU
//   Luồng của Deskhub chưa được mã hoá (PRIVACY.md nói rõ, và DTLS/AEAD vẫn là GĐ6
//   của roadmap). Gửi mật khẩu trần trên UDP nghĩa là bất kỳ ai bắt được một gói
//   duy nhất sẽ có nó vĩnh viễn — và người ta dùng lại mật khẩu ở nơi khác. Với
//   challenge-response, thứ đi trên dây là một hàm băm của (mật khẩu, nonce ngẫu
//   nhiên): bắt được nó cũng chỉ dùng được cho đúng một lần bắt tay đã trôi qua.
//
//   `nonce` do host sinh mới mỗi lần chính là để thế: không có nó, một lời đáp bắt
//   được hôm qua sẽ phát lại được hôm nay (replay).
//
//   `clientId` được trộn vào proof để lời đáp gắn với đúng client đó — không thể
//   cắt proof của máy A dán sang phiên bắt tay của máy B.
//
// PHẠM VI BẢO VỆ — ĐỌC KỸ
//   Host lưu `key` (đầu ra PBKDF2), không lưu mật khẩu. Điều đó chống được việc lộ
//   mật khẩu GỐC nếu kho khoá bị đọc — người ta không dùng lại được nó ở dịch vụ
//   khác. Nhưng `key` vẫn TƯƠNG ĐƯƠNG mật khẩu ĐỐI VỚI MÁY NÀY: ai đọc được nó thì
//   tự ký được proof. Đây là giới hạn cố hữu của challenge-response đối xứng; muốn
//   bỏ luôn giới hạn đó thì phải chuyển sang cơ chế bất đối xứng (SCRAM augmented,
//   hoặc SPAKE2/OPAQUE). Ghi ở đây để không ai đọc code này rồi tưởng nó mạnh hơn
//   thực tế.
//
//   Và nhắc lại cho rõ: lớp này KHÔNG mã hoá phiên. Sau khi bắt tay xong, video,
//   input và clipboard vẫn đi trần. Nó chặn NGƯỜI LẠ MỞ PHIÊN, không chặn người
//   nghe lén đọc phiên đang chạy.
//
// LIÊN QUAN: deskhub/crypto/Sha256.h (nguyên hàm), deskhub/auth/AuthGuard.h (trạng
//            thái phía host), deskhubp/Random.h (nonce/salt), docs/04-protocol.md §9
// =============================================================================
#include "deskhub/crypto/Sha256.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace deskhub {

inline constexpr size_t kAuthSaltBytes = 16;
inline constexpr size_t kAuthNonceBytes = 32;
inline constexpr size_t kAuthProofBytes = kSha256Bytes; // 32
inline constexpr size_t kAuthKeyBytes = kSha256Bytes;   // 32
inline constexpr size_t kAuthTokenBytes = 32;           // deviceToken

// Số vòng PBKDF2. ~100 ms trên máy để bàn hiện nay — trả MỘT LẦN lúc bắt tay, không
// nằm trên đường nóng của phiên. Giá trị đi TRÊN DÂY trong AUTH_CHALLENGE chứ không
// bị nhúng cứng hai đầu, để nâng nó lên sau này không làm hỏng client cũ.
inline constexpr uint32_t kAuthKdfIterations = 100'000;

// Trần số vòng nằm ở deskhub/wire/Wire.h (kMaxKdfIterations), không ở đây: nó là
// một ràng buộc trên DỮ LIỆU ĐẾN TỪ MẠNG, mà mọi ràng buộc loại đó thuộc về tầng
// wire — ranh giới tin cậy của chương trình. Tầng này chỉ làm toán và không biết
// giá trị iterations nó nhận được đến từ đâu.

// Khoá dẫn xuất từ mật khẩu. Host giữ nó thay cho mật khẩu; client tính lại mỗi lần
// kết nối từ mật khẩu người dùng nhập và `salt` nhận được trong AUTH_CHALLENGE.
struct AuthKey {
    uint8_t salt[kAuthSaltBytes] = {};
    uint8_t key[kAuthKeyBytes] = {};
    uint32_t iterations = kAuthKdfIterations;
    bool valid = false; // false = chưa đặt mật khẩu
};

// Dẫn xuất khoá từ mật khẩu + salt. Dùng ở CẢ HAI phía:
//   host  — một lần khi người dùng đặt mật khẩu (salt mới từ RandomBytes);
//   client— mỗi lần kết nối (salt lấy từ AUTH_CHALLENGE).
// Mật khẩu rỗng trả về AuthKey với valid = false: "không có mật khẩu" phải là một
// trạng thái tường minh, không phải một khoá dẫn từ chuỗi rỗng.
AuthKey DeriveKey(std::string_view password, std::span<const uint8_t> salt,
    uint32_t iterations = kAuthKdfIterations);

// proof = HMAC-SHA256(key, nonce ‖ clientId_be32). Ghi kAuthProofBytes vào `out`.
// Cả hai phía chạy hàm này; khác nhau chỉ ở chỗ khoá đến từ đâu.
void ComputeProof(const AuthKey& key, std::span<const uint8_t> nonce, uint32_t clientId,
    std::span<uint8_t> out);

// So proof nhận được với proof tự tính. Luôn so HẰNG THỜI GIAN (ConstantTimeEqual):
// dùng memcmp ở đây sẽ rò rỉ số byte đầu đã đúng qua thời gian chạy, và một kẻ tấn
// công kiên nhẫn dò được cả proof từng byte một.
// Trả false nếu khoá chưa đặt hoặc kích thước đầu vào sai.
bool VerifyProof(const AuthKey& key, std::span<const uint8_t> nonce, uint32_t clientId,
    std::span<const uint8_t> proof);

// Băm một deviceToken để lưu trữ. Token là 32 byte NGẪU NHIÊN nên không cần KDF
// chậm — không có gì để dò từ điển cả; SHA-256 một lượt là đủ để kho thiết bị tin
// cậy bị đọc cũng không lấy ra được token dùng lại được.
void HashToken(std::span<const uint8_t> token, std::span<uint8_t> out);

} // namespace deskhub
