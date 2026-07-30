#pragma once
// =============================================================================
// ClientLoop.h — vòng đời một phiên XEM trên macOS. Lớp trung tâm của vai client.
//                Port sát của client/ios/app/cpp/ClientLoop.h.
//
// NHIỆM VỤ
//   Nối bốn thứ thành một phiên chạy được: socket UDP, máy trạng thái ClientSession
//   của core, bộ ghép mảnh Reassembler, và bộ giải mã VtDecoder. Nguồn muốn xem do
//   caller chọn sẵn qua QuerySources().
//
// KHÁC BẢN iOS Ở ĐÂU — VÀ VÌ SAO
//   iOS là máy CẢM ỨNG: chỉ có chạm (chuột tuyệt đối), bàn phím ảo (ký tự), và một
//   thanh phím tắt. macOS là máy DESKTOP có bàn phím + chuột thật, nên kênh input ở
//   đây giàu hơn hẳn và đối ứng client Windows chứ không phải iOS:
//     - QueueKey(vk, scan, down) — nhấn/nhả RIÊNG BIỆT, không phải "tap" ghép sẵn.
//       Giữ W để nhân vật chạy là điều iOS không làm được còn desktop thì bắt buộc.
//     - QueueMouseWheel — desktop có con lăn.
//     - Chuột TƯƠNG ĐỐI dùng thật (khoá chuột bằng F9, như client Windows), không
//       chỉ là API để dành.
//     - ReleaseAllKeys() — cửa sổ mất focus giữa lúc đang giữ phím. iOS không có
//       khái niệm này vì bàn phím ảo không giữ phím.
//
// BA THREAD, VÀ LÝ DO CÓ TỪNG CÁI
//   Main   — giao/thu hồi layer, hỏi trạng thái để vẽ overlay, bơm input vào hàng đợi.
//   Net    — recvfrom → ClientSession + Reassembler → đẩy frame vào hàng đợi.
//   Decode — rút frame khỏi hàng đợi → VtDecoder → AVSampleBufferDisplayLayer.
//
//   Vì sao Net và Decode phải tách: nếu giải mã chạy ngay trên thread Net thì trong
//   lúc nó bận, recvfrom ngừng nghe, buffer UDP của hệ điều hành tràn và sinh mất
//   gói THẬT — loại mất mát mà cả FEC lẫn xin IDR đều không cứu được.
//
// HAI CƠ CHẾ ĐỒNG BỘ, ĐỪNG NHẦM LẪN
//   1. HÀNG ĐỢI FRAME (decMutex_/decCv_/decQueue_) — Net sản xuất, Decode tiêu thụ.
//      Giới hạn kMaxQueuedFrames = 3: đầy thì VỨT frame cũ nhất chứ không chặn Net.
//   2. BẮT TAY LAYER (winMutex_/winCv_/winAckCv_/winGen_/winAckGen_) — Main giao hoặc
//      thu hồi layer, và phải CHỜ Decode xác nhận đã buông. Bắt buộc: một
//      AVSampleBufferDisplayLayer bị buông trong khi decoder còn enqueue vào đó là
//      lỗi vòng đời. Đếm thế hệ để nhiều lần đổi liên tiếp không nuốt mất lần nào;
//      decodeExited_ là lối thoát chống treo khi thread Decode đã chết.
//
// LIÊN QUAN: ClientLoop.cpp, VtDecoder.h, deskhub/session/ClientSession.h,
//            deskhub/transport/Reassembler.h,
//            client/ios/app/cpp/ClientLoop.h (bản song song),
//            client/windows/ClientLoop.cpp (bản tham chiếu desktop)
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

#include "decode/VtDecoder.h"
#include "net/UdpSocket.h"

#include "deskhub/control/ClockOffset.h"
#include "deskhub/transport/Reassembler.h"
#include "deskhub/protocol/Wire.h"

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
    // `screenW`/`screenH` là cỡ MÀN HÌNH máy này tính bằng pixel, đi vào HELLO để
    // host co luồng cho vừa (deskhub::Hello::maxWidth). 0 = không biết, host chỉ dùng
    // trần của riêng nó. Cỡ MÀN HÌNH chứ không phải cỡ cửa sổ: cửa sổ co giãn được
    // mà giao thức không có đường báo lại giữa phiên.
    bool Start(const NetAddr& server, uint8_t sourceId, uint32_t screenW, uint32_t screenH);
    void Stop();

    // Giao layer mới (AVSampleBufferDisplayLayer* dưới dạng __bridge void*), hoặc
    // nullptr khi cửa sổ đóng / view biến mất. CHẶN tới khi thread Decode xác nhận
    // đã buông layer cũ — bắt buộc, vì decoder còn enqueue vào một layer đã buông là
    // lỗi vòng đời.
    void SetLayer(void* layer);

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
    // scancode PC set 1 (bit8 = cờ E0) — cả hai do MacKeyMap dịch từ NSEvent.keyCode.
    // Gửi cả hai vì host Windows cần scancode cho game DirectInput (docs/07 §5).
    void QueueKey(int32_t vk, int32_t scan, bool down);

    // Nhả MỌI phím và nút chuột đang giữ. Gọi khi cửa sổ mất focus hoặc khi tắt bắt
    // input: không có nó thì host giữ phím W mãi và nhân vật chạy không dừng — cùng
    // một lỗi mà InputInjector::ReleaseAll chống ở đầu bên kia, nhưng chống từ đây
    // rẻ hơn vì client biết ngay lúc mất focus, còn host phải đợi timeout 5 giây.
    void ReleaseAllInput();

    // Chuột tuyệt đối: `nx`/`ny` chuẩn hoá 0..65535 trong khung video — cùng hệ toạ
    // độ với InputCapture bên Windows, host map lên khung hình đã capture. Caller tự
    // chuẩn hoá theo rect của view video; ở đây chỉ kẹp biên.
    void QueueMouseMoveAbs(int32_t nx, int32_t ny);

    // Chuột TƯƠNG ĐỐI — chế độ khoá chuột cho game FPS (F9, đối ứng client Windows):
    // dx/dy là delta thô, absolute = 0, host bơm qua đường relative và game tự áp
    // sensitivity. Không kẹp biên — delta không có biên.
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
    uint32_t screenW_ = 0, screenH_ = 0; // cỡ màn hình máy này (pixel), 0 = không biết
    UdpSocket sock_;

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

    // Layer: bắt tay theo thế hệ. Main tăng winGen_, Decode ack bằng winAckGen_.
    std::mutex winMutex_;
    std::condition_variable winCv_;    // báo Decode có thay đổi
    std::condition_variable winAckCv_; // báo Main thay đổi đã được áp dụng
    void* layer_ = nullptr;            // AVSampleBufferDisplayLayer* (__bridge)
    uint64_t winGen_ = 0;
    uint64_t winAckGen_ = 0;
    bool decodeExited_ = false;

    // Hàng đợi frame Net -> Decode.
    static constexpr size_t kMaxQueuedFrames = 3;
    std::mutex decMutex_;
    std::condition_variable decCv_;
    std::deque<deskhub::Reassembler::Frame> decQueue_;

    std::atomic<bool> decodeFailed_{false};
    // Tầng hiển thị vừa nghẽn và có frame bị vứt (VtDecoder::TakeCongestionDrops).
    // TÁCH khỏi decodeFailed_ có chủ ý: decodeFailed_ nghĩa là decoder HỎNG và kéo
    // theo tháo-dựng lại cả decoder, quá nặng cho một cơn nghẽn thoáng qua. Cái này
    // chỉ cần một IDR để nối lại chuỗi tham chiếu vừa đứt.
    std::atomic<bool> displayCongested_{false};
    std::atomic<bool> queueOverflow_{false};
    std::atomic<uint32_t> stRendered_{0};

    // Input UI thread gom -> thread Net vét (cùng mô hình client Windows). Khóa chỉ
    // giữ vài chục nano giây quanh push/swap, không nằm trên đường nóng của video.
    // wantFocus_: đã từng gửi input thì phải báo host SET_FOCUS — bơm input bên host
    // chỉ tới được cửa sổ/ứng dụng đang foreground.
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
    // Frame bị vứt vì tầng hiển thị nghẽn. Trên bản Apple đây là con số DUY NHẤT
    // lộ ra ùn tắc: enqueueSampleBuffer bất đồng bộ nên dq_drop mãi bằng 0.
    std::atomic<uint32_t> dgDispDrop_{0};

    // Ước lượng trễ e2e (docs/06 §7, deskhub/control/ClockOffset.h).
    // minRttUs_: Net ghi, Decode đọc. lastE2eUs_: Decode ghi, Net đọc.
    // clockOffset_: CHỈ thread Decode chạm — nó không tự khoá, và nó phải được bơm
    // mẫu ở ĐÚNG MỘT điểm trong đường dẫn (xem AddSample).
    std::atomic<uint32_t> minRttUs_{0};
    std::atomic<int64_t> lastE2eUs_{-1};
    deskhub::ClockOffset clockOffset_;
};
