// =============================================================================
// AuthGuard.cpp — cài đặt cổng gác (xem .h về ba thứ nó quản và vì sao ở core).
//
// BỐ CỤC theo đúng thứ tự một client đi qua:
//   SetPassword  — cấu hình
//   OnHello      — cửa 1: token tin cậy? đang bị khoá? có đòi mật khẩu không?
//   BeginChallenge — ghi nonce
//   OnResponse   — cửa 2: proof đúng không?
//   Remember     — nhớ thiết bị
//   Forget / NoteFailure — phụ trợ
//
// NGUYÊN TẮC XUYÊN SUỐT: FAIL CLOSED. Mọi nhánh không chắc chắn đều trả Reject.
// Một lỗi lập trình ở đây phải dẫn tới "không ai vào được" chứ không phải "ai cũng
// vào được" — chiều sai thứ nhất bị báo ngay trong một phút dùng thử, chiều sai thứ
// hai có thể không ai phát hiện ra bao giờ.
// =============================================================================
#include "deskhub/auth/AuthGuard.h"

#include <cstring>

namespace deskhub {

void AuthGuard::SetPassword(std::string_view password, std::span<const uint8_t> salt) {
    key_ = DeriveKey(password, salt);
    // Đổi mật khẩu làm mọi thiết bị đang nhớ mất hiệu lực. Đó là ý nghĩa của việc
    // đổi mật khẩu: nếu token cũ vẫn vào được thì máy bị mất cắp hôm qua vẫn vào
    // được hôm nay, và người dùng vừa đổi mật khẩu chính vì lý do đó.
    devices_.clear();
    pending_ = Pending{};
    wrongTries_ = 0;
    lockUntilUs_ = 0;
}

TrustedDevice* AuthGuard::FindDevice(uint32_t clientId) {
    for (TrustedDevice& d : devices_)
        if (d.clientId == clientId) return &d;
    return nullptr;
}

AuthGuard::Outcome AuthGuard::OnHello(uint32_t clientId, std::span<const uint8_t> deviceToken,
    uint64_t nowUs, RejectReason& reason) {
    // Không đòi mật khẩu (chưa đặt, hoặc người dùng tắt ô đó) → mở như trước GĐ10.
    if (!requirePassword()) return Outcome::Allow;

    // Đang bị khoá: chặn TRƯỚC cả đường token tin cậy. Khoá tạm là phản ứng với việc
    // ai đó đang dò mật khẩu ngay lúc này; cho một token đi vòng qua nó nghĩa là kẻ
    // tấn công chỉ cần một token cũ là vô hiệu hoá được cả cơ chế.
    if (lockedOut(nowUs)) {
        reason = RejectReason::LockedOut;
        return Outcome::Reject;
    }

    // Thiết bị đã được nhớ và chìa đúng token → vào thẳng, không hỏi mật khẩu.
    if (deviceToken.size() == kAuthTokenBytes) {
        if (TrustedDevice* d = FindDevice(clientId)) {
            uint8_t h[kSha256Bytes];
            HashToken(deviceToken, std::span<uint8_t>(h, kSha256Bytes));
            if (ConstantTimeEqual(std::span<const uint8_t>(h, kSha256Bytes),
                    std::span<const uint8_t>(d->tokenHash, kSha256Bytes))) {
                d->lastSeenUs = nowUs;
                return Outcome::Allow;
            }
            // Token sai với một clientId ĐANG được nhớ là chuyện bất thường — hoặc
            // token đã bị thu hồi, hoặc ai đó đang mạo danh. Tính vào bộ đếm sai
            // như một lần đáp sai, rồi bắt đi đường mật khẩu.
            NoteFailure(nowUs);
            if (lockedOut(nowUs)) {
                reason = RejectReason::LockedOut;
                return Outcome::Reject;
            }
        }
    }

    return Outcome::NeedChallenge;
}

bool AuthGuard::BeginChallenge(uint32_t clientId, std::span<const uint8_t> nonce, uint64_t nowUs) {
    if (!key_.valid) return false;
    if (nonce.size() != kAuthNonceBytes) return false;
    // Nonce toàn 0 gần như chắc chắn là RandomBytes vừa thất bại mà caller không kiểm
    // tra. Nhận nó vào sẽ khiến mọi challenge dùng chung một nonce cố định — tức là
    // lời đáp phát lại được, đúng thứ nonce sinh ra để chặn.
    bool allZero = true;
    for (uint8_t b : nonce)
        if (b) {
            allZero = false;
            break;
        }
    if (allZero) return false;

    pending_.active = true;
    pending_.clientId = clientId;
    pending_.issuedUs = nowUs;
    std::memcpy(pending_.nonce, nonce.data(), kAuthNonceBytes);
    return true;
}

std::span<const uint8_t> AuthGuard::pendingNonce() const {
    if (!pending_.active) return {};
    return std::span<const uint8_t>(pending_.nonce, kAuthNonceBytes);
}

AuthGuard::Outcome AuthGuard::OnResponse(uint32_t clientId, std::span<const uint8_t> proof,
    uint64_t nowUs, RejectReason& reason) {
    if (lockedOut(nowUs)) {
        reason = RejectReason::LockedOut;
        return Outcome::Reject;
    }
    // Lời đáp không ứng với challenge nào đang chờ: gói lạc, gói của lượt bắt tay
    // trước, hoặc ai đó bắn proof bừa. Không tính vào bộ đếm sai — nếu tính, kẻ
    // ngoài mạng chỉ cần bắn ba gói rác là khoá được host khỏi chính chủ của nó.
    if (!pending_.active || pending_.clientId != clientId) {
        reason = RejectReason::AuthRequired;
        return Outcome::Reject;
    }
    // Challenge quá hạn: cũng không tính vào bộ đếm sai, vì nguyên nhân thường là
    // mất gói chứ không phải đoán mật khẩu.
    if (nowUs > pending_.issuedUs && nowUs - pending_.issuedUs > kChallengeTimeoutUs) {
        pending_ = Pending{};
        reason = RejectReason::AuthRequired;
        return Outcome::Reject;
    }

    const bool ok = VerifyProof(key_, std::span<const uint8_t>(pending_.nonce, kAuthNonceBytes),
        clientId, proof);

    // Challenge dùng MỘT LẦN, đúng hay sai cũng bỏ. Giữ lại sau một lần đáp sai
    // nghĩa là cho phép thử proof khác trên cùng nonce — tức là dò mật khẩu ngoại
    // tuyến ngay trên host, không tốn thêm một vòng mạng nào.
    pending_ = Pending{};

    if (!ok) {
        NoteFailure(nowUs);
        reason = lockedOut(nowUs) ? RejectReason::LockedOut : RejectReason::AuthFailed;
        return Outcome::Reject;
    }

    wrongTries_ = 0;
    return Outcome::Allow;
}

void AuthGuard::NoteFailure(uint64_t nowUs) {
    ++wrongTries_;
    if (!lockoutEnabled_) return;
    if (wrongTries_ >= kMaxWrongTries) {
        lockUntilUs_ = nowUs + kLockoutUs;
        wrongTries_ = 0; // đếm lại cho đợt sau khi hết khoá
    }
}

bool AuthGuard::Remember(uint32_t clientId, std::string_view name, std::string_view address,
    std::span<const uint8_t> token, uint64_t nowUs) {
    if (token.size() != kAuthTokenBytes) return false;

    TrustedDevice* d = FindDevice(clientId);
    if (!d) {
        // Danh sách đầy: bỏ mục CŨ NHẤT. Khác với HostRegistry (nơi danh sách đầy
        // thì bỏ qua máy mới để danh sách khỏi nhấp nháy), ở đây mục mới quan trọng
        // hơn — người dùng vừa gõ mật khẩu cho đúng thiết bị này, từ chối nhớ nó sẽ
        // bắt họ gõ lại mãi mãi mà không có lời giải thích nào trên giao diện.
        if (devices_.size() >= kMaxTrustedDevices) {
            size_t oldest = 0;
            for (size_t i = 1; i < devices_.size(); ++i)
                if (devices_[i].lastSeenUs < devices_[oldest].lastSeenUs) oldest = i;
            devices_.erase(devices_.begin() + std::vector<TrustedDevice>::difference_type(oldest));
        }
        devices_.push_back(TrustedDevice{});
        d = &devices_.back();
        d->clientId = clientId;
    }
    d->name.assign(name);
    d->address.assign(address);
    d->lastSeenUs = nowUs;
    HashToken(token, std::span<uint8_t>(d->tokenHash, kSha256Bytes));
    return true;
}

bool AuthGuard::Forget(uint32_t clientId) {
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (devices_[i].clientId != clientId) continue;
        devices_.erase(devices_.begin() + std::vector<TrustedDevice>::difference_type(i));
        return true;
    }
    return false;
}

} // namespace deskhub
