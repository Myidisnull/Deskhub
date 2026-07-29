#pragma once
// =============================================================================
// VideoSink.h — ranh giới giữa THREAD GIẢI MÃ và THREAD VẼ.
//
// VÌ SAO CÓ INTERFACE NÀY THAY VÌ GỌI THẲNG
//   ClientLoop.cpp là C++ thuần và cố ý không biết gì về GTK/OpenGL/EGL (đúng như
//   bản macOS không biết gì về AVFoundation — nó chỉ cầm một `void* layer`). Nhưng
//   khác macOS, ở đây thứ nhận frame KHÔNG phải một layer của hệ thống mà là một
//   đối tượng của chính ta (VideoRenderer), nên một interface trừu tượng gọn hơn
//   là truyền void* rồi ép kiểu ở hai đầu.
//
// AI SỞ HỮU CÁI GÌ
//   SubmitFrame NHẬN QUYỀN SỞ HỮU AVFrame*: người gọi không được đụng vào nó nữa,
//   người nhận có trách nhiệm av_frame_free. Quy ước này bắt buộc phải rõ vì frame
//   phần cứng giữ một VASurface đang sống — nhả sớm thì thread vẽ đọc phải bộ nhớ
//   đã bị tái dùng, nhả muộn thì cạn pool surface và decoder đứng.
//
// LIÊN QUAN: render/VideoRenderer.h (bản cài đặt), decode/AvDecoder.h,
//            ClientLoop.h (người dùng)
// =============================================================================
#include <cstdint>

class VideoSink {
public:
    virtual ~VideoSink() = default;

    // Gọi từ thread Decode. `avFrame` là AVFrame*; sink nhận quyền sở hữu.
    virtual void SubmitFrame(void* avFrame, uint64_t ptsUs) = 0;

    // VADisplay mà decoder đang dùng, để sink export được dma-buf từ surface phần
    // cứng. nullptr = decoder đang chạy bằng phần mềm, frame nằm ở RAM.
    // Gọi từ thread Decode, TRƯỚC frame đầu tiên.
    virtual void SetVaDisplay(void* vaDisplay) = 0;

    // Thread Decode sắp biến mất: nhả mọi frame đang giữ. Sau lời gọi này sink
    // không được chạm vào AVFrame nào nữa — decoder và cả VADisplay của nó sắp bị
    // huỷ, và một AVFrame phần cứng sống lâu hơn VADisplay của nó là crash.
    virtual void DropFrames() = 0;

    // --- Số liệu ngược về ClientLoop, đọc từ thread Net ---
    // Cả hai là atomic bên trong bản cài đặt, nên gọi từ thread nào cũng được.

    // Số frame ĐÃ VẼ kể từ lần gọi trước — nuôi con số fps của overlay. Đếm ở đây
    // chứ không ở chỗ giải mã là có chủ ý: frame giải xong mà chưa lên màn hình
    // thì người dùng chưa thấy, và fps phải phản ánh cái người dùng thấy.
    virtual uint32_t TakeRenderedCount() = 0;

    // PTS (đồng hồ host) của frame vừa vẽ gần nhất — mốc tính trễ e2e. 0 = chưa có.
    virtual uint64_t lastRenderedPtsUs() const = 0;
};
