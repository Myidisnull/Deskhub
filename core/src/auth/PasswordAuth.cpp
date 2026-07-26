// =============================================================================
// PasswordAuth.cpp — cài đặt bốn hàm thuần của bắt tay (xem .h về giao thức và
// phạm vi bảo vệ).
//
// Cả file không giữ trạng thái và không cấp phát. Điểm duy nhất cần chú ý là thứ tự
// byte khi trộn clientId vào proof: nó phải giống hệt ở hai đầu, nên dùng đúng quy
// ước big-endian của tầng wire chứ không phải bố cục bộ nhớ của máy — hai máy khác
// endianness sẽ tính ra hai proof khác nhau và không bao giờ bắt tay được.
// =============================================================================
#include "deskhub/auth/PasswordAuth.h"

#include <cstring>

namespace deskhub {

AuthKey DeriveKey(std::string_view password, std::span<const uint8_t> salt,
    uint32_t iterations) {
    AuthKey k;
    // Mật khẩu rỗng = "chưa đặt". Trả về valid=false thay vì dẫn xuất khoá từ chuỗi
    // rỗng: một khoá hợp lệ dẫn từ chuỗi rỗng sẽ khiến VerifyProof CHẤP NHẬN bất kỳ
    // client nào cũng biết cách tính nó — tức là mật khẩu rỗng hoá ra lại mở cửa.
    if (password.empty()) return k;
    if (salt.size() != kAuthSaltBytes) return k;
    if (iterations == 0) iterations = kAuthKdfIterations;

    std::memcpy(k.salt, salt.data(), kAuthSaltBytes);
    k.iterations = iterations;
    Pbkdf2Sha256(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(password.data()), password.size()),
        salt, iterations, std::span<uint8_t>(k.key, kAuthKeyBytes));
    k.valid = true;
    return k;
}

void ComputeProof(const AuthKey& key, std::span<const uint8_t> nonce, uint32_t clientId,
    std::span<uint8_t> out) {
    if (out.size() < kAuthProofBytes) return;
    if (!key.valid || nonce.size() != kAuthNonceBytes) {
        std::memset(out.data(), 0, kAuthProofBytes);
        return;
    }

    // nonce ‖ clientId, clientId big-endian — cùng quy ước với mọi trường số trên
    // dây (deskhub/wire/ByteOrder.h). Không include ByteOrder ở đây vì tầng auth
    // đứng độc lập với tầng wire; bốn dòng dịch bit rẻ hơn một phụ thuộc chéo.
    uint8_t msg[kAuthNonceBytes + 4];
    std::memcpy(msg, nonce.data(), kAuthNonceBytes);
    msg[kAuthNonceBytes + 0] = uint8_t(clientId >> 24);
    msg[kAuthNonceBytes + 1] = uint8_t(clientId >> 16);
    msg[kAuthNonceBytes + 2] = uint8_t(clientId >> 8);
    msg[kAuthNonceBytes + 3] = uint8_t(clientId);

    HmacSha256(std::span<const uint8_t>(key.key, kAuthKeyBytes),
        std::span<const uint8_t>(msg, sizeof(msg)), out);
}

bool VerifyProof(const AuthKey& key, std::span<const uint8_t> nonce, uint32_t clientId,
    std::span<const uint8_t> proof) {
    if (!key.valid) return false;
    if (proof.size() != kAuthProofBytes) return false;

    uint8_t expect[kAuthProofBytes];
    ComputeProof(key, nonce, clientId, std::span<uint8_t>(expect, kAuthProofBytes));
    return ConstantTimeEqual(std::span<const uint8_t>(expect, kAuthProofBytes), proof);
}

void HashToken(std::span<const uint8_t> token, std::span<uint8_t> out) {
    if (out.size() < kSha256Bytes) return;
    Sha256(token, out);
}

} // namespace deskhub
