#pragma once
// =============================================================================
// VaDisplay.h — mở MỘT VADisplay dùng chung cho cả tiến trình.
//               Đối ứng client/windows/cpp/capture/GpuSelect.h (chuỗi chọn GPU
//               NVIDIA → Intel → AMD → CPU của bản Windows).
//
// NHIỆM VỤ
//   VA-API là cửa ngõ tới bộ mã hoá phần cứng trên Linux. Mọi thứ của nó —
//   config, context, surface, image — đều treo trên một VADisplay, và VADisplay
//   phải mở từ một "render node" của DRM (/dev/dri/renderD128...).
//
// VÌ SAO MỘT VADisplay DÙNG CHUNG, KHÔNG PHẢI MỖI ENCODER MỘT CÁI
//   Surface chỉ chia sẻ được giữa các context CÙNG một VADisplay. Ta cần đúng
//   điều đó: một surface RGB import từ dma-buf phải đi qua context VPP rồi tới
//   context encode. Mở hai display là hai vũ trụ tách biệt, không truyền surface
//   qua lại được. Ngoài ra mỗi lần vaInitialize là một lần nạp driver (~vài chục
//   ms) — chia sẻ thì máy 3 màn hình vẫn chỉ trả giá một lần.
//
// VÌ SAO RENDER NODE, KHÔNG PHẢI X11/WAYLAND DISPLAY
//   libva có vaGetDisplayWl/vaGetDisplayGLX, nhưng cả hai buộc process phải có
//   kết nối tới máy chủ hiển thị và chạy đúng loại phiên. Render node
//   (vaGetDisplayDRM) không cần gì cả: nó là một file thiết bị mọi người dùng
//   trong nhóm `render` mở được, chạy như nhau trên Wayland, Xorg, hay không có
//   phiên đồ hoạ nào. Đây cũng là cách FFmpeg và GStreamer mặc định làm.
//
// CHUỖI DÒ THIẾT BỊ
//   Quét renderD128..renderD143 và lấy card ĐẦU TIÊN có entrypoint mã hoá H.264.
//   Máy laptop lai (Intel iGPU + NVIDIA dGPU) thường có hai render node, và không
//   phải node nào cũng mã hoá được — ví dụ máy chạy driver nouveau: node NVIDIA mở
//   được nhưng không có entrypoint mã hoá nào. Dò theo NĂNG LỰC thay vì lấy bừa
//   node đầu tiên là thứ tránh được ca đó. (Muốn ép một card cụ thể thì đặt biến
//   môi trường DESKHUB_VA_DEVICE=/dev/dri/renderD129.)
//
// HAI ENTRYPOINT MÃ HOÁ — PHẢI HỎI CẢ HAI
//   VAEntrypointEncSlice là đường mã hoá cổ điển (VME, chạy trên EU của GPU);
//   VAEntrypointEncSliceLP là đường "low power" (VDEnc, khối mã hoá cứng riêng).
//   Cùng cho ra H.264, khác chỗ cài đặt. Intel từ Gen11 (Ice Lake, Tiger Lake,
//   Rocket Lake — dòng UHD 7xx) đã BỎ hẳn VME cho H.264: driver iHD trên các máy
//   đó chỉ khai EncSliceLP. Chỉ hỏi EncSlice là tự loại một lớp máy hoàn toàn mã
//   hoá được. Ưu tiên EncSlice khi có (nhiều driver cho nó nhiều núm điều khiển
//   hơn), lùi về LP khi không — và encoder PHẢI dùng lại đúng entrypoint đã chọn,
//   vì vaCreateConfig kiểm tra cặp (profile, entrypoint) chứ không tự suy ra.
//
// ⚠ KHÔNG CÓ ĐƯỜNG LÙI PHẦN MỀM
//   Khác bản Windows (NVENC → Media Foundation → WARP), ở đây không có backend
//   thứ hai: máy không có VA-API mã hoá được thì vai HOST không chạy. Vai CLIENT
//   vẫn chạy bình thường (libavcodec tự lùi về giải mã phần mềm). Đây là lựa chọn
//   có ý thức — mã hoá H.264 bằng CPU ở 60fps không đạt được mục tiêu độ trễ của
//   dự án, nên thà báo lỗi rõ ràng còn hơn cho ra một trải nghiệm tệ.
//
// MÔ HÌNH LUỒNG
//   Open() an toàn khi gọi từ nhiều thread (std::call_once bên trong). handle()
//   đọc được từ mọi thread. Bản thân libva KHÔNG bảo đảm an toàn đa luồng cho các
//   lời gọi trên cùng một display, nên mỗi VaEncoder phải tự khoá phần của mình —
//   AgentLoop đã giữ encMutex quanh mọi lời gọi Encode (xem AgentLoop.cpp).
//
// LIÊN QUAN: encode/VaEncoder.h (người dùng chính), docs/17-linux-app.md §3
// =============================================================================
#include <va/va.h>

#include <string>

class VaDisplay {
public:
    static VaDisplay& Instance();

    // Dò và mở render node. Idempotent, an toàn đa luồng.
    // false = không card nào mã hoá được H.264 (xem lastError()).
    bool Open();

    VADisplay handle() const {
        return dpy_;
    }
    bool isOpen() const {
        return dpy_ != nullptr;
    }

    // "/dev/dri/renderD128" — để log và để UI nói cho người dùng biết card nào.
    const std::string& devicePath() const {
        return devicePath_;
    }
    // Chuỗi vaQueryVendorString ("Mesa Gallium driver ... radeonsi", "Intel iHD").
    const std::string& driverName() const {
        return driverName_;
    }
    // Entrypoint mã hoá đã chọn được trên card này — VAEntrypointEncSlice hoặc
    // VAEntrypointEncSliceLP. VaEncoder phải truyền chính giá trị này vào
    // vaGetConfigAttributes/vaCreateConfig. Chỉ có nghĩa khi isOpen().
    VAEntrypoint encodeEntrypoint() const {
        return encEntrypoint_;
    }
    const std::string& lastError() const {
        return lastError_;
    }

private:
    VaDisplay() = default;
    ~VaDisplay();
    VaDisplay(const VaDisplay&) = delete;
    VaDisplay& operator=(const VaDisplay&) = delete;

    VADisplay dpy_ = nullptr;
    int drmFd_ = -1;
    VAEntrypoint encEntrypoint_ = VAEntrypointEncSlice;
    std::string devicePath_, driverName_, lastError_;
};

// Có profile/entrypoint này không? Dùng để dò năng lực card và để log.
bool VaHasEntrypoint(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint);
