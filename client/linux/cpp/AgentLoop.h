#pragma once
// =============================================================================
// AgentLoop.h — vai trò HOST trên Ubuntu: giao diện gọi vào phía chia sẻ.
//               Port sát của client/macos/app/cpp/AgentLoop.h.
//
// NHIỆM VỤ
//   Khai báo đúng bốn thứ: nguồn chia sẻ (AgentSource), tuỳ chọn phiên
//   (AgentOptions), trạng thái để UI vẽ (AgentSourceStatus), và lớp chạy phiên
//   (AgentLoop). Toàn bộ phần ghép nối nằm ở AgentLoop.cpp — đọc header khối ở đó
//   để hiểu kiến trúc luồng.
//
// ⚠ KHÁC BẢN WINDOWS Ở MỘT ĐIỂM KIẾN TRÚC: KHÔNG CHẶN
//   RunAgent() bên Windows CHẶN tới hết phiên và tự mở một cửa sổ Win32 để quản
//   lý. Ở đây UI là GTK trên main thread, không thể bị chặn — nên AgentLoop::
//   Start() dựng thread Recv rồi TRẢ VỀ NGAY, và UI hỏi trạng thái qua Status()
//   theo nhịp 500ms. Giống hệt cách bản macOS làm với SwiftUI.
//
// ⚠ KHÁC BẢN macOS Ở HAI ĐIỂM
//   1. NGUỒN ĐẾN TỪ PORTAL, KHÔNG PHẢI TỪ MỘT DANH SÁCH TA TỰ LIỆT KÊ. AgentSource
//      mang `nodeId` của PipeWire (do capture/PortalScreenCast xin được) chứ không
//      phải một CGDirectDisplayID tra được bất cứ lúc nào. Hệ quả: PHẢI gọi
//      GetShareSources() trước, và người dùng đã bấm đồng ý trong hộp thoại hệ
//      thống rồi thì Start() mới có gì để làm.
//   2. AgentOptions MANG THÊM BAO HÌNH DESKTOP. InputInjector cần nó để quy đổi
//      toạ độ chuột tuyệt đối sang thang của thiết bị uinput — lý do đầy đủ ở
//      input/InputInjector.h. Tầng UI đo bằng GDK rồi truyền vào, vì phần C++
//      thuần này cố ý không biết gì về GTK.
//
// GĐ6: NHIỀU NGUỒN, MỘT CỔNG
//   Chia sẻ nhiều MÀN HÌNH cùng lúc trên MỘT cổng UDP. Mỗi màn hình có sourceId
//   riêng, và mỗi cặp (client, nguồn) là một PHIÊN ĐỘC LẬP với sessionId riêng —
//   không nhét streamId vào header video. Lý do đầy đủ ở chú thích của
//   deskhub::SourceInfo trong core/include/deskhub/protocol/Wire.h.
//
// LIÊN QUAN: AgentLoop.cpp (kiến trúc luồng + định tuyến gói),
//            capture/SourceEnum.h, capture/ScreenCapture.h, encode/VaEncoder.h,
//            input/InputInjector.h, deskhub/session/HostSession.h,
//            client/macos/app/cpp/AgentLoop.h, docs/06-transport.md §4
// =============================================================================
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// KHÔNG có `port` và KHÔNG có `allowInput` ở đây, và đó là chủ ý (chốt
// 2026-07-27): cổng là hằng số kDeskhubPort (net/UdpSocket.h), còn chuột/bàn phím
// thì LUÔN được chia sẻ. Giống hệt bản Windows và macOS.
struct AgentOptions {
    uint32_t fps = 60;
    uint32_t bitrateMbps = 20;

    // Bao hình của TOÀN BỘ desktop (mọi màn hình ghép lại), theo pixel. Tầng UI
    // đo bằng GDK. Để 0 nếu không đo được — InputInjector sẽ coi desktop chỉ có
    // đúng màn hình đang chia sẻ (xem input/InputInjector.h).
    int32_t desktopX = 0, desktopY = 0;
    uint32_t desktopW = 0, desktopH = 0;
};

// Một màn hình được chia sẻ, đúng như portal đã cấp.
struct AgentSource {
    uint32_t nodeId = 0;            // node PipeWire
    std::string name;               // tên hiện ở danh sách phía client (UTF-8)
    int32_t x = 0, y = 0;           // vị trí trong desktop toàn cục
    uint32_t width = 0, height = 0; // theo portal; kích thước THẬT lấy từ PipeWire
};

// Ảnh chụp trạng thái MỘT nguồn, cho UI vẽ. Đối ứng AgentSourceStatus bên macOS
// và SessionSourceRow bên Windows — cùng các trường có cấu trúc.
struct AgentSourceStatus {
    uint8_t sourceId = 0;
    std::string name;
    uint32_t width = 0, height = 0;
    bool viewerConnected = false;
    std::string viewerAddr; // "ip:port" của client đang xem, rỗng nếu không có
    double captureFps = 0, sendFps = 0, sendKbps = 0;
    uint32_t rttMs = 0;    // từ FEEDBACK của client; 0 = chưa có số
    bool zeroCopy = false; // capture đang đi đường dma-buf hay đường chép qua RAM
};

class AgentLoop {
public:
    // Cả hai định nghĩa trong .cpp, KHÔNG `= default` ở đây: Impl chỉ được định
    // nghĩa bên .cpp, nên mọi hàm phải hủy được unique_ptr<Impl> đều phải nằm ở
    // đó — kể cả constructor (nó cần destructor của thành viên cho đường thoát
    // ngoại lệ).
    AgentLoop();
    ~AgentLoop();
    AgentLoop(const AgentLoop&) = delete;
    AgentLoop& operator=(const AgentLoop&) = delete;

    // Mở cổng, dựng pipeline cho từng nguồn, khởi động thread Recv rồi TRẢ VỀ.
    // CHẶN tới ~vài giây (phải đợi frame đầu của từng nguồn để biết kích thước rồi
    // mới chào được trong HELLO_ACK) → gọi ngoài main thread.
    // false = không mở được cổng, phiên portal đã đóng, hoặc không nguồn nào lên hình.
    bool Start(const std::vector<AgentSource>& sources, const AgentOptions& opt);

    // Dừng phiên và chờ thread Recv thoát hẳn. Gọi được nhiều lần.
    void Stop();

    bool running() const {
        return running_.load(std::memory_order_acquire);
    }

    // Ảnh chụp trạng thái mọi nguồn còn sống. An toàn gọi từ main thread.
    std::vector<AgentSourceStatus> Status();

    // Lý do phiên không khởi động được / kết thúc, để UI báo cho người dùng.
    std::string LastError();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::atomic<bool> running_{false};
    std::mutex errMutex_;
    std::string lastError_;
};
