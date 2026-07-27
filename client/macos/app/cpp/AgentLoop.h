#pragma once
// =============================================================================
// AgentLoop.h — vai trò HOST trên macOS: giao diện gọi vào phía chia sẻ.
//               Đối ứng client/windows/AgentLoop.h.
//
// NHIỆM VỤ
//   Khai báo đúng bốn thứ: nguồn chia sẻ (AgentSource), tuỳ chọn phiên
//   (AgentOptions), trạng thái để UI vẽ (AgentSourceStatus), và lớp chạy phiên
//   (AgentLoop). Toàn bộ phần ghép nối nằm ở AgentLoop.cpp — đọc header khối ở đó
//   để hiểu kiến trúc luồng.
//
// ⚠ KHÁC BẢN WINDOWS Ở MỘT ĐIỂM KIẾN TRÚC: KHÔNG CHẶN
//   RunAgent() bên Windows CHẶN tới hết phiên và tự mở một cửa sổ Win32 để quản lý.
//   Ở đây UI là SwiftUI trên main thread, không thể bị chặn — nên AgentLoop::Start()
//   dựng thread Recv rồi TRẢ VỀ NGAY, và UI hỏi trạng thái qua Status()/StatusLine()
//   theo nhịp 500ms. Cửa sổ quản lý phiên (SessionWindow.cpp bên Windows) vì thế
//   không có bản macOS: nó là màn hình SwiftUI ShareView, và lệnh duy nhất của
//   người dùng là Stop.
//
// GĐ6: NHIỀU NGUỒN, MỘT CỔNG
//   Chia sẻ nhiều MÀN HÌNH cùng lúc trên MỘT cổng UDP (máy nhiều monitor). Mỗi
//   màn hình có sourceId riêng, và mỗi cặp (client, nguồn) là một PHIÊN ĐỘC LẬP
//   với sessionId riêng — không nhét streamId vào header video. Lý do đầy đủ ở
//   chú thích của deskhub::SourceInfo trong core/include/deskhub/protocol/Wire.h.
//
// LIÊN QUAN: AgentLoop.cpp (kiến trúc luồng + định tuyến gói),
//            capture/ScreenCapture.h, encode/VtEncoder.h, input/InputInjector.h,
//            deskhub/session/HostSession.h, client/windows/AgentLoop.h,
//            docs/06-transport.md §4
// =============================================================================
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture/CaptureTypes.h"

// KHÔNG có `port` và KHÔNG có `allowInput` ở đây, và đó là chủ ý (chốt 2026-07-27):
// cổng là hằng số kDeskhubPort (net/UdpSocket.h), còn chuột/bàn phím thì LUÔN được
// chia sẻ. Giống hệt bản Windows — xem client/windows/cpp/AgentLoop.h.
struct AgentOptions {
    uint32_t fps = 60;
    uint32_t bitrateMbps = 20;
};

// Một màn hình được chia sẻ. `name` là tên hiện ở danh sách phía client (UTF-8).
struct AgentSource {
    uint32_t displayId = 0; // CGDirectDisplayID
    std::string name;
};

// Ảnh chụp trạng thái MỘT nguồn, cho UI vẽ. Thay vai của SessionSourceRow bên
// Windows; khác ở chỗ nó mang cả số liệu (Windows in ra console).
struct AgentSourceStatus {
    uint8_t sourceId = 0;
    std::string name;
    uint32_t width = 0, height = 0;
    bool viewerConnected = false;
    bool starting = false; // đã thêm nhưng chưa có frame đầu
    double captureFps = 0, sendFps = 0, sendKbps = 0;
};

class AgentLoop {
public:
    // Cả hai định nghĩa trong .cpp, KHÔNG `= default` ở đây: Impl chỉ được định nghĩa
    // bên .cpp, nên mọi hàm phải hủy được unique_ptr<Impl> đều phải nằm ở đó — kể cả
    // constructor (nó cần destructor của thành viên cho đường thoát ngoại lệ).
    AgentLoop();
    ~AgentLoop();
    AgentLoop(const AgentLoop&) = delete;
    AgentLoop& operator=(const AgentLoop&) = delete;

    // Mở cổng, dựng pipeline cho từng nguồn, khởi động thread Recv rồi TRẢ VỀ.
    // CHẶN tới ~vài giây (phải đợi frame đầu của từng nguồn để biết kích thước rồi
    // mới chào được trong HELLO_ACK) → gọi ngoài main thread.
    // false = không mở được cổng, hoặc không nguồn nào lên hình.
    bool Start(const std::vector<AgentSource>& sources, const AgentOptions& opt);

    // Dừng phiên và chờ thread Recv thoát hẳn. Gọi được nhiều lần.
    void Stop();

    bool running() const {
        return running_.load(std::memory_order_acquire);
    }

    // Dòng trạng thái tổng cho UI ("Sharing 2 sources"), cập nhật 1s/lần.
    std::string StatusLine();

    // Ảnh chụp trạng thái mọi nguồn còn sống. An toàn gọi từ main thread.
    std::vector<AgentSourceStatus> Status();

    // Địa chỉ IPv4 của máy này để đọc cho người bên kia ("Wi-Fi (en0)\t192.168.1.5").
    // Chụp một lần lúc Start — card mạng hiếm khi đổi giữa phiên.
    std::vector<std::string> LocalAddresses();

    // KHÔNG có AddSource/RemoveSource (bỏ 2026-07-27): phiên chia sẻ TẤT CẢ màn hình
    // và danh sách chốt ở Start, nên UI chỉ đọc trạng thái rồi Stop.

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::atomic<bool> running_{false};
};
