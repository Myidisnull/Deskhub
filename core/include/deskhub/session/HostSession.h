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
//   IDLE ──HELLO(codec hợp lệ)──→ READY ──START──→ STREAMING
//     ↑                                                │
//     └──────────── BYE / timeout 5 giây ──────────────┘
//
//   IDLE      — chưa có ai. Chỉ HELLO được xử lý.
//   READY     — đã cấp sessionId và gửi HELLO_ACK, đang đợi client sẵn sàng.
//   STREAMING — client đã gửi START; từ đây mới nhận input và mới đẩy video.
//
//   v1 chỉ phục vụ MỘT client mỗi phiên: HELLO từ clientId khác trong lúc đang bận
//   bị từ chối bằng HELLO_ACK có codec = Rejected.
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
#include "deskhub/session/ClipboardAssembler.h"
#include "deskhub/wire/Wire.h"

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
    // Chỉ MỘT cửa sổ trên host được foreground tại một thời điểm, mà SendInput bơm
    // vào cửa sổ foreground — nên client xem nhiều nguồn cùng lúc chỉ điều khiển
    // được nguồn nào host đang để foreground. Caller kéo cửa sổ nguồn lên trước khi
    // true, nhả phím đang giữ khi false.
    std::function<void(bool focused)> onFocus;
    // NACK (GĐ7): client xin gửi lại các mảnh `indices` của `frameId`. Caller tra
    // RetransmitCache rồi phát lại đúng các datagram đó. `indices` chỉ hợp lệ trong
    // lúc callback chạy (trỏ vào bộ đệm stack) — chép ra nếu cần giữ.
    std::function<void(uint32_t frameId, std::span<const uint16_t> indices)> onNack;
    // INVALIDATE_REF (GĐ7): client đã bỏ hẳn `frameId`. Caller bảo encoder đừng tham
    // chiếu frame đó nữa (NvEncInvalidateRefFrames) để phục hồi bằng P-frame rẻ thay
    // vì IDR nặng; encoder không làm được thì đành force IDR như cũ.
    std::function<void(uint32_t frameId)> onInvalidateRef;
    // GĐ8: client vừa copy văn bản — caller đặt vào clipboard máy host.
    std::function<void(std::string text)> onClipboard;
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

    // Ô "Allow keyboard and mouse" ở màn hình chia sẻ. Tắt = chỉ chia sẻ hình.
    //
    // Chính sách này nằm ở ĐÂY chứ không phải ở caller vì hai lý do. Một: nó phải đi
    // vào HELLO_ACK để client biết mà không vẽ bàn phím ảo cho một phiên chỉ-xem —
    // mà chỉ HostSession mới dựng HELLO_ACK. Hai: nó là một luật về GIAO THỨC ("gói
    // INPUT_EVENT bị bỏ"), và mỗi nền tảng tự cài lại một luật giao thức là cách chắc
    // chắn nhất để chúng lệch nhau — đúng lý do core tồn tại.
    //
    // Đổi được GIỮA phiên: client đang kết nối sẽ thấy input của nó ngừng ăn. Không
    // có thông điệp nào báo việc đó ở v1; caller muốn client biết ngay thì ngắt phiên.
    void SetInputAllowed(bool on) {
        inputAllowed_.store(on, std::memory_order_relaxed);
    }
    bool inputAllowed() const {
        return inputAllowed_.load(std::memory_order_relaxed);
    }

    // Đồng bộ clipboard. MẶC ĐỊNH TẮT, và cố ý tắt: clipboard hay chứa mật khẩu và
    // mã OTP, nên "bật vì đằng nào cũng tiện" là một quyết định người dùng phải tự
    // đưa ra ("the clipboard stays off until you turn it on"). Tắt thì mảnh
    // CLIPBOARD đến bị bỏ và SendClipboard không gửi gì.
    void SetClipboardEnabled(bool on) {
        clipboardEnabled_.store(on, std::memory_order_relaxed);
    }
    bool clipboardEnabled() const {
        return clipboardEnabled_.load(std::memory_order_relaxed);
    }

    // Trả true nếu gói hợp lệ và thuộc phiên hiện tại (caller cập nhật peer addr).
    bool HandlePacket(std::span<const uint8_t> pkt, uint64_t nowUs);
    void Tick(uint64_t nowUs);

    // Gửi văn bản clipboard của máy host cho client (GĐ8), tự chia mảnh. Bỏ qua
    // nếu chưa STREAMING, text rỗng hoặc quá kMaxClipboardBytes. Gọi trên thread
    // Recv (dùng chung buf_ với các đường gửi khác).
    void SendClipboard(std::string_view utf8);

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
    void SendHelloAck(uint64_t nowUs);
    void SendReject();
    void Disconnect();

    HostCallbacks cb_;
    StreamParams offer_;
    InputReceiver input_;
    ClipboardAssembler clip_;   // ghép mảnh clipboard từ client (GĐ8)
    uint32_t clipUpdateId_ = 0; // updateId của lần SendClipboard kế tiếp
    std::atomic<State> state_{State::Idle};
    std::atomic<uint32_t> sessionId_{0};
    // Atomic vì giao diện đặt chúng từ thread của nó trong khi thread Recv đang đọc.
    std::atomic<bool> inputAllowed_{true};
    std::atomic<bool> clipboardEnabled_{false};
    uint32_t clientId_ = 0;
    uint64_t lastRecvUs_ = 0;
    uint8_t buf_[kMaxDatagram] = {}; // chỉ dùng trên thread Recv
};

} // namespace deskhub
