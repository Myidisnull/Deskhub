#pragma once
// =============================================================================
// AuthGuard.h — cổng gác phiên phía HOST: mật khẩu, khoá tạm, thiết bị tin cậy.
//
// NHIỆM VỤ
//   Đứng TRƯỚC máy trạng thái của HostSession và trả lời: "gói HELLO này được phép
//   mở phiên, phải trả lời challenge, hay bị từ chối thẳng?". Mọi trạng thái của
//   tầng xác thực nằm ở đây; phép toán thuần nằm ở deskhub/auth/PasswordAuth.h.
//
// VÌ SAO Ở CORE CHỨ KHÔNG Ở TỪNG CLIENT
//   Cùng lý do đã viết ở HostSession::SetInputAllowed: đây là một LUẬT GIAO THỨC
//   ("HELLO không kèm chứng minh hợp lệ thì không được cấp sessionId"), và một luật
//   giao thức được cài lại ở bốn nền tảng là cách chắc chắn nhất để chúng lệch
//   nhau. Lệch ở tầng bảo mật thì hậu quả không phải là hình vỡ — mà là một nền
//   tảng nào đó vô tình mở cửa.
//
// BA THỨ NÓ QUẢN
//
//   1. MẬT KHẨU. Lưu dạng AuthKey (đầu ra PBKDF2 + salt), không lưu mật khẩu gốc.
//      Bật/tắt bằng SetRequirePassword — ứng với ô "Require a password before a
//      client may connect" ở màn Settings.
//
//   2. KHOÁ TẠM SAU KHI SAI. Đếm số lần đáp sai; quá kMaxWrongTries thì khoá
//      kLockoutUs. Không có nó, mật khẩu 6 ký tự bị dò xong trong vài phút vì UDP
//      cho thử lại nhanh tuỳ ý — đây là thứ biến "có mật khẩu" thành "mật khẩu có
//      tác dụng". Ứng với "Lock out after 3 wrong tries" và cặp số 0/3 · 5 min.
//
//   3. THIẾT BỊ TIN CẬY. Sau lần đáp đúng đầu tiên, host cấp một deviceToken ngẫu
//      nhiên 32 byte và nhớ BĂM của nó. Lần sau client chìa token ra trong HELLO là
//      vào thẳng, không phải hỏi mật khẩu người dùng nữa. Ứng với danh sách
//      "Trusted devices" và nút Forget.
//
// GIỚI HẠN CÓ CHỦ Ý CỦA v1: MỘT CHALLENGE ĐANG CHỜ
//   HostSession v1 chỉ phục vụ một client mỗi phiên, nên ở đây cũng chỉ giữ MỘT
//   challenge dở dang. Client thứ hai gõ cửa giữa chừng sẽ ghi đè challenge của
//   client thứ nhất, và client thứ nhất phải bắt tay lại. Đó là hành vi đúng với mô
//   hình một-client, và nó cũng chặn luôn việc một kẻ tấn công mở hàng nghìn
//   challenge song song để bào bộ nhớ host.
//
// MÔ HÌNH LUỒNG
//   Thuần C++20, không khoá, không đồng hồ (nowUs bơm từ ngoài), không cấp phát
//   trên đường xác thực. Dùng trên MỘT thread (thread Recv của host) — trừ các hàm
//   Set*/Forget* mà giao diện gọi, caller phải tự đưa về thread Recv giống như
//   Beacon::SetSources.
//
//   Nonce, salt và token đều do CALLER cấp (từ deskhubp/Random.h) chứ không sinh ở
//   đây: core/ không được đụng OS, mà entropy chỉ xin được từ nhân hệ điều hành.
//
// LIÊN QUAN: deskhub/auth/PasswordAuth.h, deskhub/session/HostSession.h (người
//            dùng), deskhubp/Random.h, docs/04-protocol.md §9
// =============================================================================
#include "deskhub/auth/PasswordAuth.h"
#include "deskhub/wire/Wire.h" // RejectReason — nó đi trên dây nên thuộc tầng wire

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace deskhub {

// Một thiết bị đã từng đáp đúng và được nhớ. Ứng với một hàng ở danh sách
// "Trusted devices". `tokenHash` là SHA-256 của token đã cấp — token gốc chỉ tồn
// tại đúng một lần, lúc gửi cho client trong HELLO_ACK.
struct TrustedDevice {
    uint32_t clientId = 0;
    std::string name;    // tên thiết bị do client khai, chỉ để hiển thị
    std::string address; // địa chỉ lần cuối, chỉ để hiển thị
    uint64_t lastSeenUs = 0;
    uint8_t tokenHash[kSha256Bytes] = {};
};

class AuthGuard {
public:
    enum class Outcome : uint8_t {
        Allow,         // vào thẳng — không đòi mật khẩu, hoặc token tin cậy khớp
        NeedChallenge, // caller gửi AUTH_CHALLENGE rồi chờ AUTH_RESPONSE
        Reject,        // caller gửi HELLO_ACK từ chối kèm `reason`
    };

    // Ngưỡng lấy đúng từ màn Settings ("0 / 3", "5 min").
    static constexpr uint32_t kMaxWrongTries = 3;
    static constexpr uint64_t kLockoutUs = 5 * 60 * 1'000'000ull;
    // Challenge chưa được đáp trong chừng này thì bỏ. Đủ rộng cho PBKDF2 phía client
    // (~100 ms) cộng vài lần mất gói, đủ hẹp để một challenge bỏ dở không chắn client
    // thật lâu hơn một nhịp bắt tay.
    static constexpr uint64_t kChallengeTimeoutUs = 30'000'000;
    static constexpr size_t kMaxTrustedDevices = 32;

    // ---- Cấu hình (giao diện gọi) ----

    // Đặt mật khẩu mới. `salt` phải là kAuthSaltBytes byte NGẪU NHIÊN mới — dùng
    // lại salt cũ làm mất tác dụng chống bảng tra dựng sẵn.
    // Mật khẩu rỗng = xoá mật khẩu (key.valid trở thành false).
    void SetPassword(std::string_view password, std::span<const uint8_t> salt);

    // Nạp lại khoá đã lưu sẵn (từ keychain) mà không cần mật khẩu gốc — đường dùng
    // bình thường lúc khởi động app.
    void SetKey(const AuthKey& key) {
        key_ = key;
    }
    const AuthKey& key() const {
        return key_;
    }
    bool hasPassword() const {
        return key_.valid;
    }

    // Ô "Require a password before a client may connect". Bật mà chưa đặt mật khẩu
    // thì KHÔNG chặn ai cả — chặn được sẽ là một host không ai vào nổi và người dùng
    // không hiểu vì sao; giao diện có trách nhiệm không cho bật khi chưa có mật khẩu.
    void SetRequirePassword(bool on) {
        requirePassword_ = on;
    }
    bool requirePassword() const {
        return requirePassword_ && key_.valid;
    }

    // Ô "Lock out after 3 wrong tries".
    void SetLockoutEnabled(bool on) {
        lockoutEnabled_ = on;
    }
    bool lockoutEnabled() const {
        return lockoutEnabled_;
    }

    // Ô "Ask again before granting mouse and keyboard". Bật = phiên mới bắt đầu ở
    // chế độ CHỈ XEM kể cả khi host cho phép điều khiển, tới khi người dùng ở host
    // duyệt riêng. AuthGuard chỉ GIỮ cờ này; việc hỏi là của giao diện, và việc nhả
    // quyền là HostSession::GrantInput.
    void SetAskBeforeInput(bool on) {
        askBeforeInput_ = on;
    }
    bool askBeforeInput() const {
        return askBeforeInput_;
    }

    // ---- Đường bắt tay (HostSession gọi) ----

    // Bước 1 — HELLO vừa tới. `deviceToken` là token client chìa ra (rỗng nếu
    // không có). Đặt `reason` khi trả về Reject.
    Outcome OnHello(uint32_t clientId, std::span<const uint8_t> deviceToken, uint64_t nowUs,
        RejectReason& reason);

    // Bước 2 — ghi nonce cho challenge sắp gửi. `nonce` phải là kAuthNonceBytes byte
    // ngẫu nhiên MỚI (deskhubp::RandomBytes). Trả false nếu chưa đặt mật khẩu hoặc
    // nonce sai kích thước; caller khi đó phải từ chối chứ không được gửi challenge.
    bool BeginChallenge(uint32_t clientId, std::span<const uint8_t> nonce, uint64_t nowUs);

    // Nonce đang chờ — caller đọc để dựng gói AUTH_CHALLENGE.
    std::span<const uint8_t> pendingNonce() const;
    uint32_t iterations() const {
        return key_.iterations;
    }
    std::span<const uint8_t> salt() const {
        return std::span<const uint8_t>(key_.salt, kAuthSaltBytes);
    }

    // Bước 3 — AUTH_RESPONSE vừa tới. Trả Allow hoặc Reject (kèm reason).
    // Đáp đúng: xoá challenge, xoá bộ đếm sai. Đáp sai: tăng bộ đếm, có thể khoá.
    Outcome OnResponse(uint32_t clientId, std::span<const uint8_t> proof, uint64_t nowUs,
        RejectReason& reason);

    // Bước 4 (tuỳ chọn) — nhớ thiết bị này. `token` là kAuthTokenBytes byte ngẫu
    // nhiên mới; caller gửi token GỐC cho client trong HELLO_ACK và chỉ giữ lại băm.
    // Trả false nếu token sai kích thước hoặc danh sách đã đầy.
    bool Remember(uint32_t clientId, std::string_view name, std::string_view address,
        std::span<const uint8_t> token, uint64_t nowUs);

    // ---- Trạng thái cho giao diện ----

    const std::vector<TrustedDevice>& devices() const {
        return devices_;
    }
    bool Forget(uint32_t clientId);
    void ForgetAll() {
        devices_.clear();
    }

    uint32_t wrongTries() const {
        return wrongTries_;
    }
    bool lockedOut(uint64_t nowUs) const {
        return lockUntilUs_ != 0 && nowUs < lockUntilUs_;
    }
    // Thời gian còn bị khoá (µs), 0 nếu không bị khoá — giao diện đếm ngược.
    uint64_t lockRemainingUs(uint64_t nowUs) const {
        return lockedOut(nowUs) ? lockUntilUs_ - nowUs : 0;
    }
    // Người dùng ở host bấm bỏ khoá thủ công.
    void ClearLockout() {
        lockUntilUs_ = 0;
        wrongTries_ = 0;
    }

    // Phiên đóng — bỏ challenge dở. KHÔNG đụng bộ đếm sai hay danh sách tin cậy:
    // xoá bộ đếm ở đây sẽ biến "ngắt rồi thử lại" thành cách lách khoá tạm.
    void ResetChallenge() {
        pending_ = Pending{};
    }

private:
    TrustedDevice* FindDevice(uint32_t clientId);
    void NoteFailure(uint64_t nowUs);

    struct Pending {
        bool active = false;
        uint32_t clientId = 0;
        uint64_t issuedUs = 0;
        uint8_t nonce[kAuthNonceBytes] = {};
    };

    AuthKey key_;
    bool requirePassword_ = false;
    bool lockoutEnabled_ = true;
    bool askBeforeInput_ = false;
    Pending pending_;
    uint32_t wrongTries_ = 0;
    uint64_t lockUntilUs_ = 0;
    std::vector<TrustedDevice> devices_;
};

} // namespace deskhub
