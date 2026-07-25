#pragma once
// =============================================================================
// LocalInputMonitor.h — "host thắng": theo dõi chuột/phím VẬT LÝ của người ngồi máy.
//                       Đối ứng client/windows/input/LocalInputMonitor.h.
//
// VẤN ĐỀ NÓ GIẢI QUYẾT
//   Hai người cùng điều khiển một máy thì input trộn thẳng vào nhau: con trỏ bị
//   giằng qua lại, phím bổ trợ lây chéo (chủ máy đang giữ Command thật + người từ xa
//   gõ Q = thoát ứng dụng). Quy ước: NGƯỜI NGỒI TẠI MÁY THẮNG. Vừa động chuột hay
//   bàn phím thật là input từ xa nhường trong kQuietUs.
//
// ⚠ LỌC CHÍNH INPUT MÌNH BƠM RA — ĐÂY LÀ TOÀN BỘ ĐỘ KHÓ
//   Sự kiện do InputInjector bơm bằng CGEventPost quay TRỞ LẠI qua bộ theo dõi này y
//   như sự kiện thật. Không lọc thì mỗi phím từ xa tự đánh dấu "người ngồi máy vừa
//   gõ", và kênh điều khiển tự khoá chính nó vĩnh viễn.
//
//   Cách lọc: InputInjector đóng dấu kUserData vào trường kCGEventSourceUserData của
//   MỌI sự kiện nó bơm; ở đây ta bỏ qua sự kiện mang dấu đó. Trường này do ứng dụng
//   tự do dùng và đi kèm sự kiện suốt vòng đời của nó — đúng vai của cờ
//   LLMHF_INJECTED bên Windows, chỉ khác là ta phải tự đóng dấu.
//
// VÌ SAO NSEvent GLOBAL MONITOR CHỨ KHÔNG PHẢI CGEventTap
//   Cả hai đều cần quyền Accessibility (đằng nào cũng đã có, vì bơm input cũng cần).
//   NSEvent monitor gắn vào run loop chính có sẵn của app, không phải dựng thêm
//   thread + run loop riêng như CGEventTap; và nó CHỈ NGHE, không bao giờ nuốt được
//   sự kiện của người dùng — thuộc tính quan trọng cho một cơ chế chạy ngầm.
//
// MÔ HÌNH LUỒNG
//   Start/Stop gọi từ thread bất kỳ (tự chuyển sang main queue vì monitor phải gắn
//   vào run loop chính). lastLocalUs() đọc được từ mọi thread — nó là atomic.
//
// LIÊN QUAN: agent/InputInjector.h (nơi tiêu thụ + nơi đóng dấu kUserData),
//            client/windows/input/LocalInputMonitor.h (bản song song)
// =============================================================================
#include <atomic>
#include <cstdint>

class LocalInputMonitor {
public:
    // Dấu đóng vào kCGEventSourceUserData của mọi sự kiện Deskhub bơm ra. Giá trị
    // tuỳ ý, chỉ cần khác 0 và khó trùng với ứng dụng khác cùng dùng trường này.
    static constexpr int64_t kUserData = 0x4445534B'48554200LL; // "DESKHUB\0"

    // Thời gian nhường sau mỗi lần người ngồi máy động vào chuột/phím thật.
    // 1 giây: đủ dài để một câu gõ liên tục không bị xen kẽ input từ xa, đủ ngắn để
    // người điều khiển không tưởng là mất kết nối.
    static constexpr uint64_t kQuietUs = 1'000'000;

    LocalInputMonitor() = default;
    ~LocalInputMonitor();
    LocalInputMonitor(const LocalInputMonitor&) = delete;
    LocalInputMonitor& operator=(const LocalInputMonitor&) = delete;

    void Start();
    void Stop();

    // NowUs() của lần cuối phát hiện input vật lý; 0 = chưa lần nào.
    uint64_t lastLocalUs() const {
        return lastUs_.load(std::memory_order_relaxed);
    }

    // true nếu người ngồi tại máy vừa dùng chuột/phím trong kQuietUs vừa qua.
    bool LocalActive(uint64_t nowUs) const {
        const uint64_t t = lastLocalUs();
        return t != 0 && nowUs - t < kQuietUs;
    }

private:
    std::atomic<uint64_t> lastUs_{0};
    // id trả về từ addGlobalMonitorForEventsMatchingMask, giữ dưới dạng void* để
    // header này include được từ file C++ thuần (AgentLoop.cpp).
    void* monitor_ = nullptr;
};
