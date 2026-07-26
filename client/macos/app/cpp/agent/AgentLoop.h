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
//   không có bản macOS: nó là màn hình SwiftUI ShareView, và mọi lệnh của người
//   dùng đi qua AddSource/RemoveSource/Stop.
//
// GĐ6: NHIỀU NGUỒN, MỘT CỔNG
//   Chia sẻ nhiều cửa sổ và/hoặc cả màn hình cùng lúc trên MỘT cổng UDP. Mỗi nguồn
//   có sourceId riêng, và mỗi cặp (client, nguồn) là một PHIÊN ĐỘC LẬP với
//   sessionId riêng — không nhét streamId vào header video. Lý do đầy đủ ở chú
//   thích của deskhub::SourceInfo trong core/include/deskhub/wire/Wire.h.
//
// LIÊN QUAN: AgentLoop.cpp (kiến trúc luồng + định tuyến gói),
//            agent/ScreenCapture.h, agent/VtEncoder.h, agent/InputInjector.h,
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

#include "agent/CaptureTypes.h"

struct AgentOptions {
    uint16_t port = 47777;
    uint32_t fps = 60;
    uint32_t bitrateMbps = 20;
    bool allowInput = true;      // GĐ4: cho client điều khiển máy này
    bool shareClipboard = false; // GĐ9: đồng bộ clipboard — MẶC ĐỊNH TẮT, người dùng tự bật
};

// Một nguồn được chia sẻ. `name` là tên hiện ở danh sách phía client (UTF-8).
struct AgentSource {
    CaptureTarget target;
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

    // Cổng THẬT đang nghe — có thể khác opt.port nếu cổng đó đã bận (xem .cpp).
    uint16_t port() const {
        return port_.load(std::memory_order_relaxed);
    }

    // Dòng trạng thái tổng cho UI ("Sharing 2 sources on port 47777"), cập nhật 1s/lần.
    std::string StatusLine();

    // Ảnh chụp trạng thái mọi nguồn còn sống. An toàn gọi từ main thread.
    std::vector<AgentSourceStatus> Status();

    // Địa chỉ IPv4 của máy này để đọc cho người bên kia ("Wi-Fi (en0)\t192.168.1.5").
    // Chụp một lần lúc Start — card mạng hiếm khi đổi giữa phiên.
    std::vector<std::string> LocalAddresses();

    // Thêm/bớt nguồn GIỮA PHIÊN (ô tick sống trên ShareView). Cả hai
    // chỉ đặt lệnh vào hộp thư; thread Recv thi hành ở vòng kế tiếp — cùng mô hình
    // SessionWindow::TakeAdds/TakeRemoves bên Windows.
    void AddSource(const AgentSource& s);
    void RemoveSource(uint8_t sourceId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::atomic<bool> running_{false};
    std::atomic<uint16_t> port_{0};
};
