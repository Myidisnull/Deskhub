// =============================================================================
// HostSession.cpp — cài đặt máy trạng thái và bộ định tuyến thông điệp phía host.
//
// HandlePacket là một switch lớn trên loại thông điệp, và mọi nhánh (trừ HELLO)
// đều mở đầu bằng CÙNG MỘT ĐÔI KIỂM TRA:
//
//     if (state() == ... || !InSession(h->sessionId)) return false;
//     lastRecvUs_ = nowUs;
//
//   - Kiểm tra trạng thái: thông điệp đến sai giai đoạn thì bỏ. Ví dụ INPUT_EVENT
//     lúc chưa STREAMING là vô nghĩa vì host còn chưa biết client là ai.
//   - Kiểm tra sessionId: chặn gói lạc từ phiên cũ hoặc từ máy khác trên mạng.
//     UDP không có kết nối, ai cũng gửi tới cổng này được, nên đây là hàng rào duy
//     nhất phân biệt "client của tôi" với phần còn lại của Internet.
//   - lastRecvUs_ = nowUs: mọi gói hợp lệ đều NUÔI TIMEOUT. Chỉ cần client còn nói
//     chuyện, bất kể nói gì, phiên vẫn sống.
//
// GIÁ TRỊ TRẢ VỀ CÓ Ý NGHĨA RIÊNG
//   true không chỉ là "gói hợp lệ" — AgentLoop dùng nó làm tín hiệu cập nhật địa
//   chỉ peer theo địa chỉ nguồn của gói (client đổi IP khi chuyển Wi-Fi/4G vẫn giữ
//   được phiên). Vì thế BYE trả về false dù nó là gói hoàn toàn hợp lệ: phiên vừa
//   đóng, cập nhật peer theo nó là vô nghĩa.
//
// LIÊN QUAN: deskhub/session/HostSession.h (máy trạng thái + lý do thiết kế),
//            ClientSession.cpp (đầu kia)
// =============================================================================
#include "deskhub/session/HostSession.h"

#include <cstring>

namespace deskhub {

bool HostSession::HandlePacket(std::span<const uint8_t> pkt, uint64_t nowUs) {
    const auto h = ParseCommonHeader(pkt);
    if (!h) return false;
    const auto payload = PayloadOf(pkt);

    switch (h->type) {
        case MsgType::Hello: {
            const auto m = ParseHello(payload);
            if (!m) return false;
            const State st = state();
            if ((st == State::Ready || st == State::Streaming) && m->clientId != clientId_) {
                SendReject(RejectReason::Busy); // v1: một client mỗi phiên
                return false;
            }
            if (st == State::Idle || st == State::Authenticating) {
                // v1 chỉ phát H.264. Client không giải mã được thì từ chối ngay ở bước
                // bắt tay, thay vì để nó ngồi nhìn màn hình đen không hiểu vì sao.
                if (!(m->codecMask & kCodecMaskH264)) {
                    SendReject(RejectReason::CodecMismatch);
                    return false;
                }

                // Cổng mật khẩu (GĐ10). Ba lối ra, và lối mặc định là TỪ CHỐI.
                RejectReason reason = RejectReason::None;
                const auto outcome = auth_.OnHello(m->clientId, m->deviceToken, nowUs, reason);
                if (outcome == AuthGuard::Outcome::Reject) {
                    SendReject(reason);
                    return false;
                }

                clientId_ = m->clientId;
                clientName_ = m->deviceName;
                lastRecvUs_ = nowUs;

                if (outcome == AuthGuard::Outcome::NeedChallenge) {
                    // Mỗi HELLO (kể cả bản phát lại) sinh một challenge MỚI. Client
                    // phát lại HELLO mỗi 0.5 giây khi chưa thấy ACK, nên đây cũng là
                    // cơ chế tự lành khi challenge hoặc lời đáp bị mất gói.
                    state_.store(State::Authenticating, std::memory_order_release);
                    SendAuthChallenge(nowUs);
                    return true;
                }
                if (!BeginSession(nowUs)) return false;
                SendHelloAck(nowUs);
                return true;
            }
            // READY/STREAMING cùng client = HELLO phát lại (mất ACK) → gửi lại ACK.
            lastRecvUs_ = nowUs;
            SendHelloAck(nowUs);
            return true;
        }
        case MsgType::AuthResponse: {
            // Chỉ có nghĩa khi đang chờ đúng lời đáp này. Ở trạng thái khác, gói này
            // là gói lạc của lượt bắt tay trước hoặc ai đó bắn proof bừa — bỏ im lặng,
            // KHÔNG cho nó nuôi timeout của một phiên đang chạy.
            if (state() != State::Authenticating) return false;
            const auto m = ParseAuthResponse(payload);
            if (!m || m->clientId != clientId_) return false;
            lastRecvUs_ = nowUs;

            RejectReason reason = RejectReason::None;
            const auto outcome = auth_.OnResponse(m->clientId,
                std::span<const uint8_t>(m->proof, kAuthProofBytes), nowUs, reason);
            if (outcome != AuthGuard::Outcome::Allow) {
                SendReject(reason);
                // Về IDLE: challenge đã bị tiêu, giữ AUTHENTICATING chỉ tổ chặn
                // client khác trong khi ở đây không còn gì để chờ.
                state_.store(State::Idle, std::memory_order_release);
                clientId_ = 0;
                return false;
            }

            // Đáp đúng → nhớ thiết bị này để lần sau khỏi hỏi mật khẩu. Token gốc chỉ
            // đi trên dây ĐÚNG MỘT LẦN, ngay trong HELLO_ACK dưới đây.
            havePendingToken_ = false;
            if (cb_.randomBytes &&
                cb_.randomBytes(std::span<uint8_t>(pendingToken_, kAuthTokenBytes))) {
                if (auth_.Remember(clientId_, clientName_, {},
                        std::span<const uint8_t>(pendingToken_, kAuthTokenBytes), nowUs)) {
                    havePendingToken_ = true;
                    if (cb_.onTrustedDevicesChanged) cb_.onTrustedDevicesChanged();
                }
            }
            if (!BeginSession(nowUs)) return false;
            SendHelloAck(nowUs);
            return true;
        }
        case MsgType::Start:
            if (!InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            // Client phát lại START tới khi thấy video nên gói này hay đến nhiều lần;
            // chỉ lần đầu mới đổi trạng thái và gọi onStart (nó ép encoder ra IDR —
            // gọi lặp sẽ làm luồng hình giật vì IDR nặng gấp nhiều lần P-frame).
            if (state() != State::Streaming) {
                state_.store(State::Streaming, std::memory_order_release);
                if (cb_.onStart) cb_.onStart();
            }
            return true;
        case MsgType::Ping: {
            if (!InSession(h->sessionId)) return false;
            const auto m = ParsePingPong(payload);
            if (!m) return false;
            lastRecvUs_ = nowUs;
            const size_t n = BuildPong(buf_, sessionId(), *m);
            if (n && cb_.send) cb_.send(std::span<const uint8_t>(buf_, n));
            return true;
        }
        case MsgType::RequestKeyframe:
            if (state() != State::Streaming || !InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            if (cb_.onKeyframeRequest) cb_.onKeyframeRequest();
            return true;
        case MsgType::InputEvent:
            // Chỉ nhận input khi đang STREAMING: trước đó host chưa biết client là ai.
            if (state() != State::Streaming || !InSession(h->sessionId)) return false;
            // Gói vẫn NUÔI TIMEOUT dù bị bỏ: một phiên chỉ-xem mà người dùng ngồi rê
            // chuột suốt vẫn là một phiên sống, và HELLO_ACK đã nói trước với client
            // rằng input không được nhận (kAckFlagInputAccepted).
            lastRecvUs_ = nowUs;
            if (!inputAllowed()) return true;
            input_.HandlePacket(payload, cb_.onInput);
            return true;
        case MsgType::SetFocus: {
            if (state() != State::Streaming || !InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            const auto focused = ParseSetFocus(payload);
            if (focused && cb_.onFocus) cb_.onFocus(*focused);
            return true;
        }
        case MsgType::Feedback: {
            if (!InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            const auto m = ParseFeedback(payload);
            if (m && cb_.onFeedback) cb_.onFeedback(*m);
            return true; // gói vẫn nuôi timeout kể cả khi payload hỏng
        }
        case MsgType::Nack: {
            if (state() != State::Streaming || !InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            uint16_t idx[kMaxNackIndices];
            uint32_t frameId = 0;
            const size_t n = ParseNack(payload, frameId, idx);
            if (n && cb_.onNack) cb_.onNack(frameId, std::span<const uint16_t>(idx, n));
            return true;
        }
        case MsgType::InvalidateRef: {
            if (state() != State::Streaming || !InSession(h->sessionId)) return false;
            lastRecvUs_ = nowUs;
            const auto fid = ParseInvalidateRef(payload);
            if (fid && cb_.onInvalidateRef) cb_.onInvalidateRef(*fid);
            return true;
        }
        case MsgType::Clipboard: {
            if (state() != State::Streaming || !InSession(h->sessionId)) return false;
            const auto c = ParseClipboardChunk(payload);
            if (!c) return false;
            lastRecvUs_ = nowUs;
            // Tắt đồng bộ clipboard = bỏ mảnh, KHÔNG ghép dở dang. Ghép rồi mới bỏ
            // ở bước áp dụng thì văn bản của người ta vẫn nằm trong bộ nhớ máy này.
            if (!clipboardEnabled()) return true;
            // Push trả văn bản đúng một lần khi đủ mảnh — mảnh trùng/lẻ là nullopt.
            if (auto text = clip_.Push(*c); text && cb_.onClipboard)
                cb_.onClipboard(std::move(*text));
            return true;
        }
        case MsgType::Bye:
            if (!InSession(h->sessionId)) return false;
            Disconnect();
            return false; // phiên đã đóng — đừng cập nhật peer theo gói này
        default:
            return false;
    }
}

// Gọi đều đặn từ vòng lặp Recv. Việc duy nhất: phát hiện client biến mất không kịp
// nói lời chào. UDP không báo đứt kết nối, nên im lặng quá kSessionTimeoutUs là dấu
// hiệu duy nhất ta có — có thể client rút mạng, tắt máy, hoặc sập nguồn.
void HostSession::Tick(uint64_t nowUs) {
    if (state() == State::Idle) return;
    if (nowUs - lastRecvUs_ > kSessionTimeoutUs) Disconnect();
}

// Chia văn bản thành mảnh ≤ kMaxClipboardChunk rồi phát một loạt — cùng chính sách
// best-effort với ClientSession::SendClipboard (xem ghi chú ở đó).
void HostSession::SendClipboard(std::string_view utf8) {
    if (state() != State::Streaming || !cb_.send) return;
    if (!clipboardEnabled()) return;
    if (utf8.empty() || utf8.size() > kMaxClipboardBytes) return;
    const uint32_t id = ++clipUpdateId_;
    const size_t count = (utf8.size() + kMaxClipboardChunk - 1) / kMaxClipboardChunk;
    for (size_t i = 0; i < count; ++i) {
        const size_t off = i * kMaxClipboardChunk;
        const size_t len = utf8.size() - off < kMaxClipboardChunk ? utf8.size() - off
                                                                  : kMaxClipboardChunk;
        const auto* d = reinterpret_cast<const uint8_t*>(utf8.data()) + off;
        const size_t n = BuildClipboardChunk(buf_, sessionId(), id, uint16_t(i),
            uint16_t(count), std::span<const uint8_t>(d, len));
        if (n) cb_.send(std::span<const uint8_t>(buf_, n));
    }
}

// Cấp sessionId và mở phiên. Gọi từ hai chỗ: HELLO khi không đòi mật khẩu, và
// AUTH_RESPONSE khi đáp đúng.
//
// sessionId lấy từ CSPRNG chứ không trộn từ đồng hồ như trước GĐ10. Cách cũ
// (nowUs ^ (nowUs>>32) ^ clientId) có hai điểm yếu cộng dồn: clientId do chính bên
// kia chọn, và bit thấp của đồng hồ đơn điệu đoán được trong phạm vi hẹp — mà
// sessionId lại là hàng rào duy nhất chặn gói giả (xem InSession).
bool HostSession::BeginSession(uint64_t nowUs) {
    uint32_t sid = 0;
    if (cb_.randomBytes) {
        uint8_t b[4];
        if (cb_.randomBytes(std::span<uint8_t>(b, 4)))
            sid = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
    }
    if (!sid) {
        // Không có entropy → KHÔNG mở phiên. Lùi về sessionId dẫn từ đồng hồ sẽ dựng
        // lại đúng điểm yếu vừa bỏ đi, và tệ hơn là dựng lại nó ở một đường hiếm khi
        // chạy nên không ai thử tới. Fail closed, và caller thấy ngay vì không kết
        // nối được (nối cb_.randomBytes vào deskhubp::RandomBytes là xong).
        SendReject(RejectReason::AuthRequired);
        state_.store(State::Idle, std::memory_order_release);
        clientId_ = 0;
        havePendingToken_ = false;
        return false;
    }
    sessionId_.store(sid, std::memory_order_relaxed);
    // Phiên mới bắt đầu chỉ-xem nếu người dùng bật "Ask again before granting mouse
    // and keyboard"; ngược lại kế thừa ô Allow như trước.
    inputGranted_.store(!auth_.askBeforeInput(), std::memory_order_relaxed);
    lastRecvUs_ = nowUs;
    state_.store(State::Ready, std::memory_order_release);
    auth_.ResetChallenge();
    return true;
}

// AUTH_CHALLENGE: salt + số vòng KDF + một nonce ngẫu nhiên dùng đúng một lần.
// Không lấy được entropy thì TỪ CHỐI — gửi challenge với nonce toàn 0 nghĩa là mọi
// lời đáp bắt được đều phát lại được, tức là lớp mật khẩu chỉ còn là trang trí.
void HostSession::SendAuthChallenge(uint64_t nowUs) {
    uint8_t nonce[kAuthNonceBytes];
    if (!cb_.randomBytes || !cb_.randomBytes(std::span<uint8_t>(nonce, kAuthNonceBytes))) {
        SendReject(RejectReason::AuthRequired);
        state_.store(State::Idle, std::memory_order_release);
        clientId_ = 0;
        return;
    }
    if (!auth_.BeginChallenge(clientId_, std::span<const uint8_t>(nonce, kAuthNonceBytes), nowUs)) {
        SendReject(RejectReason::AuthRequired);
        state_.store(State::Idle, std::memory_order_release);
        clientId_ = 0;
        return;
    }

    AuthChallenge c;
    const auto salt = auth_.salt();
    std::memcpy(c.salt, salt.data(), salt.size());
    c.iterations = auth_.iterations();
    std::memcpy(c.nonce, nonce, kAuthNonceBytes);
    const size_t n = BuildAuthChallenge(buf_, c);
    if (n && cb_.send) cb_.send(std::span<const uint8_t>(buf_, n));
}

void HostSession::SendHelloAck(uint64_t nowUs) {
    HelloAck a;
    a.sessionId = sessionId();
    a.codec = Codec::H264;
    a.width = offer_.width;
    a.height = offer_.height;
    a.fps = offer_.fps;
    a.bitrateBps = offer_.bitrateBps;
    a.timebaseUs = nowUs; // mốc đồng hồ host để client ước lượng trễ e2e (§7)
    // Nói trước cho client biết phiên này được phép làm gì, để nó khỏi vẽ bàn phím ảo
    // và nút khoá chuột cho một phiên chỉ-xem (xem kAckFlag* trong Wire.h).
    a.flags = uint16_t((inputAllowed() ? kAckFlagInputAccepted : 0) |
                       (clipboardEnabled() ? kAckFlagClipboard : 0));
    // Token thiết bị đi kèm ĐÚNG MỘT LẦN, trong ACK ngay sau khi đáp đúng mật khẩu.
    // Các ACK phát lại sau đó (client chưa thấy ACK đầu) không mang lại token: nó đã
    // ở chỗ client rồi, và mỗi lần một bí mật lên dây là thêm một dịp bị bắt được.
    if (havePendingToken_) {
        a.deviceToken.assign(pendingToken_, pendingToken_ + kAuthTokenBytes);
        havePendingToken_ = false;
    }
    const size_t n = BuildHelloAck(buf_, a);
    if (n && cb_.send) cb_.send(std::span<const uint8_t>(buf_, n));
}

// Từ chối = HELLO_ACK với codec = Rejected, mọi trường khác để 0. Dùng lại chính
// thông điệp ACK thay vì thêm một loại "REJECT" riêng: client vốn đã phải chờ ACK,
// nên nó nhận được câu trả lời dứt khoát ngay thay vì đợi hết 10 giây rồi bỏ cuộc.
// `reason` để client hiện đúng thông báo — "sai mật khẩu" và "máy đang bận" đòi hai
// hành động hoàn toàn khác nhau từ người dùng.
void HostSession::SendReject(RejectReason reason) {
    HelloAck a{};
    a.codec = Codec::Rejected;
    a.reason = reason;
    const size_t n = BuildHelloAck(buf_, a);
    if (n && cb_.send) cb_.send(std::span<const uint8_t>(buf_, n));
}

void HostSession::Disconnect() {
    state_.store(State::Idle, std::memory_order_release);
    sessionId_.store(0, std::memory_order_relaxed);
    clientId_ = 0;
    clientName_.clear();
    havePendingToken_ = false;
    input_.Reset(); // client sau bắt đầu lại từ seq 0
    // Bỏ challenge dở. KHÔNG đụng bộ đếm sai hay danh sách tin cậy — xoá bộ đếm ở
    // đây sẽ biến "ngắt rồi nối lại" thành cách lách khoá tạm (xem AuthGuard).
    auth_.ResetChallenge();
    // Quên clipboard đang ghép dở: client sau đánh updateId lại từ đầu, giữ trạng
    // thái cũ thì mảnh đầu của nó có thể bị nhận nhầm là "bản đã áp dụng".
    clip_ = ClipboardAssembler{};
    if (cb_.onDisconnect) cb_.onDisconnect(); // caller nhả hết phím đang giữ
}

} // namespace deskhub
