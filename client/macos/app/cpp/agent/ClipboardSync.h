#pragma once
// =============================================================================
// ClipboardSync.h — đồng bộ clipboard VĂN BẢN của máy này với đầu kia (GĐ8).
//                   Đối ứng client/windows/ClipboardSync.h.
//
// NHIỆM VỤ
//   Hai chiều, và cả hai đều đi qua lớp này:
//     máy này copy  → callback onLocalCopy → caller gửi qua phiên
//     đầu kia copy  → SetRemoteText()      → đặt vào NSPasteboard máy này
//
// VÌ SAO PHẢI HỎI VÒNG (POLLING) CHỨ KHÔNG CÓ SỰ KIỆN
//   macOS KHÔNG có thông báo "clipboard vừa đổi" cho ứng dụng nền — NSPasteboard chỉ
//   cho một bộ đếm `changeCount` tăng mỗi lần có ai đó ghi vào. Nên cách duy nhất là
//   so bộ đếm theo nhịp. Windows có chuỗi cửa sổ nghe WM_CLIPBOARDUPDATE nên bản
//   Windows không cần thread riêng; ở đây thì cần.
//
// ⚠ KHỬ VÒNG LẶP — ĐÂY LÀ CHỖ DỄ SAI NHẤT
//   Đặt văn bản của đầu kia vào clipboard máy này làm changeCount TĂNG, và vòng hỏi
//   kế tiếp sẽ tưởng "người dùng vừa copy" rồi gửi ngược lại. Đầu kia nhận, đặt vào
//   clipboard của nó, gửi lại... một cú copy thành vòng lặp vô tận.
//   Chống bằng hai lớp: (a) nhớ changeCount NGAY sau khi tự ghi, bỏ qua đúng lần
//   tăng đó; (b) so nội dung với bản vừa nhận — trùng thì không gửi. Lớp (b) là lưới
//   an toàn cho trường hợp có ứng dụng khác ghi xen vào giữa.
//
// GIỚI HẠN CÓ CHỦ Ý: CHỈ VĂN BẢN
//   Ảnh và file đi qua clipboard thì phải chuyển hàng MB qua kênh control best-effort
//   — sai kênh. Văn bản là 95% nhu cầu thực tế và vừa vài datagram (kMaxClipboardBytes
//   = 64 KB, xem Wire.h).
//
// MÔ HÌNH LUỒNG
//   Start dựng một thread nền hỏi vòng 300ms. onLocalCopy chạy TRÊN thread đó —
//   caller chỉ được đặt vào hộp thư, đừng làm việc dài. SetRemoteText gọi từ thread
//   bất kỳ; nó tự chuyển việc chạm NSPasteboard sang main queue.
//
// LIÊN QUAN: deskhub/session/ClipboardAssembler.h (ghép mảnh), agent/AgentLoop.cpp,
//            client/windows/ClipboardSync.h (bản song song)
// =============================================================================
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class ClipboardSync {
public:
    // Gọi khi NGƯỜI DÙNG máy này copy văn bản mới. Chạy trên thread hỏi vòng.
    using LocalCopyHandler = std::function<void(const std::string& utf8)>;

    ClipboardSync() = default;
    ~ClipboardSync();
    ClipboardSync(const ClipboardSync&) = delete;
    ClipboardSync& operator=(const ClipboardSync&) = delete;

    void Start(LocalCopyHandler onLocalCopy);
    void Stop();

    // Đầu kia vừa copy — đặt vào clipboard máy này. Tự khử vòng lặp (xem đầu file).
    void SetRemoteText(const std::string& utf8);

private:
    void PollThread();

    std::thread thread_;
    std::atomic<bool> quit_{false};
    LocalCopyHandler onLocalCopy_;

    // changeCount đã thấy, và bản văn bản vừa nhận từ đầu kia — hai lớp khử vòng lặp.
    std::mutex mutex_;
    int64_t lastChangeCount_ = 0;
    std::string lastRemoteText_;
};
