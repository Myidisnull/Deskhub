#pragma once
// =============================================================================
// ClientLoop.h — vòng đời một phiên XEM trên Ubuntu. Lớp trung tâm của vai client.
//                Port sát của client/macos/app/cpp/ClientLoop.h.
//
// NHIỆM VỤ
//   Nối bốn thứ thành một phiên chạy được: socket UDP, máy trạng thái ClientSession
//   của core, bộ ghép mảnh Reassembler, và bộ giải mã AvDecoder. Nguồn muốn xem do
//   caller chọn sẵn qua QuerySources().
//
// KÊNH INPUT GIỐNG BẢN macOS
//   Ubuntu cũng là máy DESKTOP có bàn phím + chuột thật, nên kênh input ở đây đối
//   ứng client Windows/macOS chứ không phải mobile:
//     - QueueKey(vk, scan, down) — nhấn/nhả RIÊNG BIỆT. Giữ W để nhân vật chạy là
//       điều bàn phím ảo không làm được còn desktop thì bắt buộc.
//     - QueueMouseWheel — desktop có con lăn.
//     - Chuột TƯƠNG ĐỐI (khoá chuột bằng F9) dùng thật.
//     - ReleaseAllInput() — cửa sổ mất focus giữa lúc đang giữ phím.
//
// BA THREAD, VÀ LÝ DO CÓ TỪNG CÁI
//   Main   — bơm input vào hàng đợi, hỏi trạng thái để vẽ overlay (GTK main loop).
//   Net    — recvfrom → ClientSession + Reassembler → đẩy frame vào hàng đợi.
//   Decode — rút frame khỏi hàng đợi → AvDecoder → VideoSink.
//
//   Vì sao Net và Decode phải tách: nếu giải mã chạy ngay trên thread Net thì trong
//   lúc nó bận, recvfrom ngừng nghe, buffer UDP của hệ điều hành tràn và sinh mất
//   gói THẬT — loại mất mát mà cả FEC lẫn xin IDR đều không cứu được.
//
// ⚠ KHÁC BẢN macOS: KHÔNG CÓ BẮT TAY GIAO/THU LAYER
//   Bản macOS phải có cả một cơ chế đếm thế hệ (winGen_/winAckGen_) vì
//   AVSampleBufferDisplayLayer thuộc SwiftUI và có thể biến mất bất cứ lúc nào
//   trong khi thread Decode còn enqueue vào nó.
//
//   Ở đây `sink` là VideoRenderer — MỘT ĐỐI TƯỢNG CỦA CHÍNH TA, do ViewerWindow sở
//   hữu, và hợp đồng đơn giản hơn hẳn: SINK PHẢI SỐNG LÂU HƠN ClientLoop. Thứ tự
//   huỷ bắt buộc ở ViewerWindow là Stop() (join cả hai thread) TRƯỚC khi huỷ
//   renderer. Nhờ vậy bỏ được toàn bộ cơ chế bắt tay — ít trạng thái hơn, ít chỗ
//   sai hơn. Đổi lại: ĐỪNG bao giờ huỷ renderer khi phiên còn chạy.
//
// HAI CƠ CHẾ ĐỒNG BỘ CÒN LẠI
//   1. HÀNG ĐỢI FRAME (decMutex_/decCv_/decQueue_) — Net sản xuất, Decode tiêu thụ.
//      Giới hạn kMaxQueuedFrames = 3: đầy thì VỨT frame cũ nhất chứ không chặn Net.
//   2. HÀNG ĐỢI INPUT (inputMutex_) — Main gom, Net vét mỗi vòng.
//
// LIÊN QUAN: ClientLoop.cpp, decode/AvDecoder.h, render/VideoSink.h,
//            deskhub/session/ClientSession.h, deskhub/transport/Reassembler.h,
//            client/macos/app/cpp/ClientLoop.h (bản song song),
//            gtk/ViewerWindow.cpp (chủ sở hữu)
// =============================================================================
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "net/UdpSocket.h"

#include "deskhub/protocol/Wire.h"
#include "deskhub/transport/Reassembler.h"

class VideoSink;

class ClientLoop {
public:
    // Trạng thái cho tầng UI hiển thị. UI chỉ cần biết "đang quay bánh xe" hay "đã
    // có hình", không cần Hello/Starting của ClientSession.
    enum class Phase : int32_t { Idle = 0,
        Connecting = 1,
        Streaming = 2,
        Ended = 3 };

    ClientLoop() = default;
    ~ClientLoop();
    ClientLoop(const ClientLoop&) = delete;
    ClientLoop& operator=(const ClientLoop&) = delete;

    // `sourceId` lấy từ SOURCE_LIST (xem net/SourceQuery.h); 0 = nguồn đầu tiên.
    // `sink` PHẢI sống lâu hơn ClientLoop — xem ⚠ ở đầu file.
    bool Start(const NetAddr& server, uint8_t sourceId, VideoSink* sink);
    void Stop();

    Phase phase() const {
        return phase_.load(std::memory_order_acquire);
    }

    // Dòng số liệu cho overlay (fps/kbps/RTT/e2e), cập nhật 1s/lần. Chuỗi rỗng khi
    // chưa có số liệu. Có khóa vì UI thread đọc còn thread Net ghi.
    std::string StatusLine();

    // Lý do phiên kết thúc, để UI báo cho người dùng thay vì im lặng.
    std::string EndReason();

    // --- Kênh input. Tất cả gọi từ UI thread; thread Net vét hàng đợi mỗi vòng rồi
    // giao ClientSession đánh seq và gửi lặp chống kẹt phím (InputSender). Chỉ có
    // tác dụng khi phiên đang STREAMING. ---

    // Một lần nhấn HOẶC nhả phím vật lý. `vk` là mã phím ảo Windows, `scan` là
    // scancode PC set 1 (bit8 = cờ E0) — cả hai do LinuxKeyMap dịch từ mã evdev.
    // Gửi cả hai vì host Windows cần scancode cho game DirectInput (docs/07 §5).
    void QueueKey(int32_t vk, int32_t scan, bool down);

    // Nhả MỌI phím và nút chuột đang giữ. Gọi khi cửa sổ mất focus: không có nó thì
    // host giữ phím W mãi và nhân vật chạy không dừng — cùng một lỗi mà
    // InputInjector::ReleaseAll chống ở đầu bên kia, nhưng chống từ đây rẻ hơn vì
    // client biết ngay lúc mất focus, còn host phải đợi timeout 5 giây.
    void ReleaseAllInput();

    // Chuột tuyệt đối: `nx`/`ny` chuẩn hoá 0..65535 trong khung video — cùng hệ toạ
    // độ với mọi client khác, host map lên khung hình đã capture. Caller tự chuẩn
    // hoá theo rect của vùng video; ở đây chỉ kẹp biên.
    void QueueMouseMoveAbs(int32_t nx, int32_t ny);

    // Chuột TƯƠNG ĐỐI — chế độ khoá chuột cho game FPS (F9): dx/dy là delta thô,
    // absolute = 0, host bơm qua đường relative và game tự áp sensitivity. Không
    // kẹp biên — delta không có biên.
    void QueueMouseMoveRel(int32_t dx, int32_t dy);

    // Nhấn/nhả một nút chuột tại vị trí con trỏ hiện hành. `button` theo
    // deskhub::MouseButton (1 = trái, 2 = phải, 3 = giữa).
    void QueueMouseButton(int32_t button, bool down);

    // Con lăn. `delta` là bội của 120 như WHEEL_DELTA của Windows (dương = cuộn lên).
    void QueueMouseWheel(int32_t delta);

    // Kích thước video đàm phán được — UI dùng để đặt đúng tỉ lệ khung.
    uint32_t videoWidth() const {
        return negW_.load();
    }
    uint32_t videoHeight() const {
        return negH_.load();
    }

private:
    void NetThread();
    void DecodeThread();
    // Xếp một event vào hàng đợi input. GỌI KHI ĐANG GIỮ inputMutex_.
    void PushLocked(const deskhub::InputEvent& e);

    NetAddr server_{};
    uint8_t sourceId_ = 0;
    UdpSocket sock_;
    VideoSink* sink_ = nullptr;

    std::thread netThread_;
    std::thread decodeThread_;

    std::atomic<bool> quit_{false};
    std::atomic<Phase> phase_{Phase::Idle};

    // Chuỗi hiển thị: thread Net ghi, UI thread đọc.
    std::mutex textMutex_;
    std::string statusLine_;
    std::string endReason_;

    // Tham số đàm phán được (thread Net ghi, thread Decode đọc).
    std::atomic<uint32_t> negW_{0}, negH_{0};
    std::atomic<bool> rebuildDecoder_{false}; // RECONFIG -> dựng lại decoder

    // Hàng đợi frame Net -> Decode.
    static constexpr size_t kMaxQueuedFrames = 3;
    std::mutex decMutex_;
    std::condition_variable decCv_;
    std::deque<deskhub::Reassembler::Frame> decQueue_;

    std::atomic<bool> decodeFailed_{false};
    std::atomic<bool> queueOverflow_{false};

    // Input UI thread gom -> thread Net vét (cùng mô hình client Windows/macOS).
    // Khóa chỉ giữ vài chục nano giây quanh push/swap, không nằm trên đường nóng
    // của video. wantFocus_: đã từng gửi input thì phải báo host SET_FOCUS.
    std::mutex inputMutex_;
    std::vector<deskhub::InputEvent> inputQueue_;
    std::atomic<bool> wantFocus_{false};
    // Phím/nút ĐANG GIỮ, để ReleaseAllInput biết phải nhả cái gì. Khoá chung
    // inputMutex_ vì luôn được sửa cùng lúc với inputQueue_.
    std::map<int32_t, int32_t> keysDown_; // vk -> scan
    std::set<int32_t> buttonsDown_;

    // --- Chẩn đoán (docs/09): t_dec của cửa sổ 1s. Thread Decode ghi, thread Net
    // đọc-và-reset. ---
    std::atomic<uint32_t> dgDecMsSum_{0}, dgDecMsMax_{0}, dgDecCount_{0};

    // Ước lượng trễ e2e (docs/06 §7): Net ghi và Net đọc, nhưng để atomic cho
    // thống nhất với bản macOS.
    std::atomic<int64_t> ackDeltaUs_{0};
    std::atomic<uint32_t> minRttUs_{0};
};
