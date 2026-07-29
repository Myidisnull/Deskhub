#pragma once
// =============================================================================
// LocalInputMonitor.h — "host thắng": theo dõi chuột/phím VẬT LÝ của người ngồi máy.
//                       Đối ứng client/windows/cpp/input/LocalInputMonitor.h và
//                       client/macos/.../input/LocalInputMonitor.h.
//
// VẤN ĐỀ NÓ GIẢI QUYẾT
//   Hai người cùng điều khiển một máy thì input trộn thẳng vào nhau: con trỏ bị
//   giằng qua lại, phím bổ trợ lây chéo (chủ máy đang giữ Ctrl thật + người từ xa
//   gõ W = đóng tab của chủ máy). Quy ước: NGƯỜI NGỒI TẠI MÁY THẮNG. Vừa động
//   chuột hay bàn phím thật là input từ xa nhường trong kQuietUs.
//
// ⚠ LỌC CHÍNH INPUT MÌNH BƠM RA — ĐÂY LÀ TOÀN BỘ ĐỘ KHÓ
//   Thiết bị ảo của InputInjector là thiết bị /dev/input/event* y như bàn phím
//   thật — chính vì thế compositor mới nhận nó. Nhưng nghĩa là bộ theo dõi này
//   cũng thấy nó. Không lọc thì mỗi phím từ xa tự đánh dấu "người ngồi máy vừa
//   gõ", và kênh điều khiển tự khoá chính nó vĩnh viễn.
//
//   Cách lọc: bỏ qua mọi thiết bị có TÊN bắt đầu bằng "Deskhub"
//   (InputInjector::kKeyboardName...). Đơn giản hơn hẳn hai nền tảng kia —
//   Windows dựa vào cờ LLMHF_INJECTED của hệ điều hành, macOS phải tự đóng dấu
//   vào kCGEventSourceUserData — vì ở đây thiết bị là của ta và ta đặt tên nó.
//
// ⚠ CẦN QUYỀN ĐỌC /dev/input/event*
//   Mặc định các file này thuộc nhóm `input` và chỉ nhóm đó đọc được. Cùng lớp
//   quyền với /dev/uinput mà InputInjector đã cần, nên một quy tắc udev lo cả
//   hai (docs/17-linux-app.md §7). KHÔNG mở được thì lớp này trở nên VÔ HẠI chứ
//   không phải lỗi: LocalActive() luôn trả false, input từ xa không bị chặn, chỉ
//   là mất tính năng nhường quyền. Nói ra một lần trong log rồi thôi.
//
// VÌ SAO ĐỌC evdev TRỰC TIẾP CHỨ KHÔNG DÙNG libinput
//   libinput là thư viện dành cho compositor: nó muốn ĐỘC QUYỀN thiết bị và có
//   một mô hình seat/context khá nặng. Ta chỉ cần một câu trả lời duy nhất —
//   "vừa có ai chạm vào phần cứng chưa?" — nên đọc thẳng evdev vừa nhẹ hơn vừa
//   không giành thiết bị với compositor (mở O_RDONLY không lấy grab).
//
// MÔ HÌNH LUỒNG
//   Start/Stop gọi từ thread bất kỳ (Start dựng một thread nền riêng).
//   lastLocalUs()/LocalActive() đọc được từ mọi thread — chúng là atomic.
//
// LIÊN QUAN: input/InputInjector.h (nơi tiêu thụ + nơi đặt tên thiết bị ảo),
//            client/macos/.../input/LocalInputMonitor.h (bản song song)
// =============================================================================
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

class LocalInputMonitor {
public:
    // Thời gian nhường sau mỗi lần người ngồi máy động vào chuột/phím thật.
    // 1 giây: đủ dài để một câu gõ liên tục không bị xen kẽ input từ xa, đủ ngắn
    // để người điều khiển không tưởng là mất kết nối. Cùng con số với hai nền
    // tảng kia — đừng đổi lệch.
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
    void Run();
    // Mở/đóng lại danh sách thiết bị. Gọi lúc bắt đầu và định kỳ (cắm thêm bàn
    // phím giữa phiên là chuyện thường).
    void Rescan();
    void CloseAll();

    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::atomic<uint64_t> lastUs_{0};
    // Chỉ thread nền chạm sau khi Start().
    std::vector<int> fds_;
    bool warnedNoAccess_ = false;
};
