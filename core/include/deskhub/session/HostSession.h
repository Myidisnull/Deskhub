#pragma once
// =============================================================================
// HostSession.h — máy trạng thái của một phiên, phía HOST (Agent).
//
// NHIỆM VỤ
//   Giữ trạng thái "đang có client nào, phiên số mấy, đã bắt đầu phát chưa" và xử
//   lý toàn bộ kênh Control đến từ client. Đây là bộ não của phía host; AgentLoop
//   chỉ lo phần cơ bắp (socket, encoder, capture) và phản ứng qua callback.
//
// MÁY TRẠNG THÁI
//
//   IDLE ──HELLO──→ READY ──START──→ STREAMING
//     ↑                │                 │
//     │       BYE / timeout 5 giây       │
//     └────────────────┴─────────────────┘
//
//   IDLE      — chưa có ai. Chỉ HELLO được xử lý.
//   READY     — đã cấp sessionId và gửi HELLO_ACK, đang đợi client sẵn sàng.
//   STREAMING — client đã gửi START; từ đây mới nhận input và mới đẩy video.
//
//   v1 chỉ phục vụ MỘT client mỗi phiên: HELLO từ clientId khác trong lúc đang bận
//   bị từ chối bằng HELLO_ACK có codec = Rejected kèm reason = Busy.
//
// ⚠ sessionId = 0 KHÔNG BAO GIỜ uỷ quyền cho gói nào — xem InSession() ở phần
//   private: ở IDLE sessionId vẫn là 0, phép so trần sẽ nhận nhầm gói giả sid=0.
//
// VÌ SAO TÁCH KHỎI SOCKET, THREAD, ĐỒNG HỒ
//   Byte vào qua HandlePacket, byte ra qua callback `send`, thời gian bơm từ ngoài
//   (`nowUs`). Nhờ vậy toàn bộ logic bắt tay, timeout, khử trùng input đều kiểm
//   chứng được trong CoreTests mà không cần mở cổng mạng hay chờ đồng hồ thật.
//   Caller (AgentLoop) sở hữu socket và địa chỉ peer; HandlePacket trả true khi gói
//   hợp lệ thuộc phiên → caller cập nhật peer theo địa chỉ nguồn (roaming §1.5).
//
// MÔ HÌNH LUỒNG
//   HandlePacket/Tick chạy trên MỘT thread (Recv). Riêng state()/sessionId() phải
//   đọc được từ thread khác — thread encode hỏi "đã STREAMING chưa?" trước mỗi
//   frame — nên hai trường đó là std::atomic. Mọi trường còn lại chỉ thread Recv
//   chạm tới, kể cả bộ đệm buf_.
//
// LIÊN QUAN: deskhub/session/ClientSession.h (đầu kia), deskhub/input/InputReceiver.h,
//            client/windows/AgentLoop.cpp (người dùng), docs/04-protocol.md
// =============================================================================
#include "deskhub/input/InputReceiver.h"
#include "deskhub/protocol/Wire.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace deskhub {

inline constexpr uint64_t kSessionTimeoutUs = 5'000'000; // 5s không gói → mất peer

struct StreamParams {
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t fps = 60;
    uint32_t bitrateBps = 20'000'000;
};

struct HostCallbacks {
    std::function<void(std::span<const uint8_t>)> send; // giao datagram cho tầng socket
    std::function<void()> onStart;                      // nhận START → force IDR, bắt đầu đẩy video
    std::function<void()> onKeyframeRequest;            // REQUEST_KEYFRAME từ client
    std::function<void()> onDisconnect;                 // BYE hoặc timeout → đã quay về IDLE
    // FEEDBACK từ client (~1s/lần): mất gói, RTT, bitrate nhận thực tế. Caller
    // dùng để siết/nới bitrate encoder (GĐ5). Số liệu là của cửa sổ 1s vừa qua.
    std::function<void(const Feedback&)> onFeedback;
    // Event input đã khử trùng, đúng thứ tự (GĐ4). Caller bơm vào InputInjector.
    // LƯU Ý: onDisconnect phải nhả hết phím/nút đang giữ — mất kết nối giữa lúc
    // giữ phím mà không nhả sẽ kẹt phím ở máy host.
    std::function<void(const InputEvent&)> onInput;
    // SET_FOCUS: client vừa chuyển sang (true) hoặc rời khỏi (false) nguồn này.
    // Việc duy nhất caller phải làm là NHẢ HẾT PHÍM đang giữ khi false — người dùng
    // rời đi giữa lúc giữ phím thì gói key-up không bao giờ tới, không nhả là kẹt
    // phím ở máy host. true không đòi hành động gì (vai trò cũ "kéo cửa sổ nguồn
    // lên foreground" đã bỏ cùng share-theo-cửa-sổ 2026-07-27).
    std::function<void(bool focused)> onFocus;
    // NACK (GĐ7): client xin gửi lại các mảnh `indices` của `frameId`. Caller tra
    // RetransmitCache rồi phát lại đúng các datagram đó. `indices` chỉ hợp lệ trong
    // lúc callback chạy (trỏ vào bộ đệm stack) — chép ra nếu cần giữ.
    std::function<void(uint32_t frameId, std::span<const uint16_t> indices)> onNack;
    // INVALIDATE_REF (GĐ7): client đã bỏ hẳn `frameId`. Caller bảo encoder đừng tham
    // chiếu frame đó nữa (NvEncInvalidateRefFrames) để phục hồi bằng P-frame rẻ thay
    // vì IDR nặng; encoder không làm được thì đành force IDR như cũ.
    std::function<void(uint32_t frameId)> onInvalidateRef;
    // Nguồn ngẫu nhiên MÃ HOÁ, dùng để cấp sessionId. Caller nối vào
    // deskhubp::RandomBytes (core không đụng OS được, mà entropy chỉ xin được từ
    // nhân hệ điều hành).
    //
    // Trả false = thất bại. Khi đó HostSession TỪ CHỐI phiên chứ không đi tiếp với
    // một sessionId toàn số 0/đoán được: sessionId là hàng rào duy nhất chặn gói
    // giả chen vào phiên (xem InSession). Không nối callback này thì mọi kết nối
    // bị từ chối — fail closed.
    std::function<bool(std::span<uint8_t>)> randomBytes;
};

class HostSession {
public:
    enum class State : uint8_t { Idle,
        Ready,
        Streaming };

    HostSession(HostCallbacks cb, StreamParams offer)
        : cb_(std::move(cb)), offer_(offer) {}

    // Cửa sổ nguồn đổi kích thước / bitrate bị siết: HELLO_ACK sau này phải mang số
    // mới, không thì client kết nối lại sẽ dựng decoder theo kích thước đã chết.
    // Gửi RECONFIG cho client đang chạy là việc của caller (nó giữ địa chỉ peer).
    void SetOffer(const StreamParams& p) {
        offer_ = p;
    }

    // Trả true nếu gói hợp lệ và thuộc phiên hiện tại (caller cập nhật peer addr).
    bool HandlePacket(std::span<const uint8_t> pkt, uint64_t nowUs);
    void Tick(uint64_t nowUs);

    State state() const {
        return state_.load(std::memory_order_acquire);
    }
    uint32_t sessionId() const {
        return sessionId_.load(std::memory_order_relaxed);
    }
    const InputReceiver::Stats& inputStats() const {
        return input_.stats();
    }

private:
    // Gói này có thuộc phiên ĐANG CHẠY không.
    //
    // Điều kiện `cur != 0` là phần thiết yếu, không phải phòng xa: sessionId bằng 0
    // ở IDLE, nên phép so trần `h->sessionId != sessionId()` sẽ ĐÚNG với một gói
    // giả mang sessionId = 0 gửi vào đúng lúc đang bắt tay — và một START như thế
    // sẽ đẩy thẳng host sang STREAMING. Mọi nhánh của HandlePacket đi qua đây thay
    // vì tự so.
    bool InSession(uint32_t sid) const {
        const uint32_t cur = sessionId();
        return cur != 0 && sid == cur;
    }

    void SendHelloAck(uint64_t nowUs);
    void SendReject(RejectReason reason);
    // Cấp sessionId và chuyển sang READY. Trả false nếu không lấy được entropy —
    // caller phải từ chối phiên chứ không đi tiếp với sessionId đoán được.
    bool BeginSession(uint64_t nowUs);
    void Disconnect();

    HostCallbacks cb_;
    StreamParams offer_;
    InputReceiver input_;
    std::atomic<State> state_{State::Idle};
    std::atomic<uint32_t> sessionId_{0};
    uint32_t clientId_ = 0;
    uint64_t lastRecvUs_ = 0;
    uint8_t buf_[kMaxDatagram] = {}; // chỉ dùng trên thread Recv
};

} // namespace deskhub
