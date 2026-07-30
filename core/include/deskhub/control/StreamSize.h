#pragma once
// =============================================================================
// StreamSize.h — chọn ĐỘ PHÂN GIẢI để phát, phía HOST.
//
// NHIỆM VỤ
//   Trả lời một câu hỏi duy nhất, một lần lúc bắt tay: "màn hình nguồn WxH, người
//   dùng đặt trần T, client nói màn nó CxD — vậy nên nén ở cỡ nào?". Vào là bốn số,
//   ra là một cỡ khung chẵn. Không đụng capture, không đụng encoder, không đọc đồng
//   hồ.
//
// VÌ SAO CẦN NÓ — HOST GỬI DƯ PIXEL LÀ TỰ BẮN VÀO CHÂN
//   Màn Mac là Retina: MacBook Pro 14" cho 3024×1964 = 5.9 Mpixel, gấp gần ba lần
//   1080p; màn XDR 6K là 20 Mpixel. Host Windows/Ubuntu cắm màn 4K cũng vậy. Ở 60fps
//   và bitrate mặc định 20 Mbps thì 5.9 Mpixel chỉ còn 0.06 bit/pixel — quá ít để ra
//   hình xem được, mà vẫn đủ nặng để làm nghẹt bộ mã hoá phần cứng và thổi mỗi IDR
//   lên hàng trăm datagram bắn liên tiếp, tràn buffer gửi, mất gói, BitrateController
//   tụt rate. Hình vừa mờ vừa giật, và mọi pixel dư đều bị client vẽ xuống một khung
//   bé hơn rồi vứt đi.
//
// HAI TRẦN, LẤY CÁI CHẶT HƠN
//   1. TRẦN NGƯỜI DÙNG (`maxDim`) — cạnh dài không vượt quá ngần này. Người dùng biết
//      đường truyền của mình; 0 = "cứ gửi native, tôi chịu được".
//   2. TRẦN CLIENT (`clientW`×`clientH`) — client báo cỡ MÀN HÌNH nó trong HELLO. Gửi
//      to hơn thứ nó vẽ nổi là phí thuần tuý. 0×0 = client đời cũ không nói gì.
//
//   Cả hai đều GIỮ NGUYÊN TỈ LỆ nguồn. Sai tỉ lệ chỉ tạo viền đen mà ta vẫn phải nén
//   và vẫn trả tiền băng thông cho.
//
// ⚠ TRẦN CLIENT DÙNG CẠNH DÀI LÀM BỀ RỘNG, CÓ CHỦ Ý
//   Điện thoại xoay được, và ta chốt cỡ MỘT LẦN lúc HELLO (không có đường client báo
//   ngược khi xoay máy). Nên phải tính theo hướng ngốn pixel nhất — máy nằm ngang —
//   chứ không phải hướng nó đang cầm lúc kết nối. Tính theo hướng dọc thì người dùng
//   xoay ngang là hình mờ hẳn và không có gì sửa được cho tới khi kết nối lại.
//
// LIÊN QUAN: deskhub/protocol/Wire.h (Hello::maxWidth/maxHeight — nguồn của CxD),
//            deskhub/control/BitrateController.h (nút chỉnh còn lại của cùng bài toán),
//            docs/04-protocol.md §HELLO
// =============================================================================
#include <cstdint>

namespace deskhub {

struct StreamSize {
    uint32_t width = 0;
    uint32_t height = 0;

    friend bool operator==(const StreamSize& a, const StreamSize& b) {
        return a.width == b.width && a.height == b.height;
    }
};

// Cỡ khung nên phát, đã co giữ tỉ lệ và làm tròn XUỐNG số chẵn (H.264 lấy mẫu chroma
// theo khối 2×2 nên cạnh lẻ bị bộ mã hoá từ chối).
//
//   srcW/srcH        cỡ thật của màn hình nguồn, tính bằng pixel. 0 → trả {0,0}.
//   maxDim           trần cạnh dài do người dùng đặt; 0 = không trần.
//   clientW/clientH  cỡ màn hình client báo trong HELLO; 0 = không biết, bỏ qua.
//
// KHÔNG BAO GIỜ PHÓNG TO: nguồn đã nhỏ hơn mọi trần thì trả lại y nguyên. Phóng to ở
// host chỉ đốt bitrate cho pixel nội suy mà client tự làm được và làm tốt hơn.
StreamSize FitStreamSize(uint32_t srcW, uint32_t srcH, uint32_t maxDim, uint32_t clientW,
    uint32_t clientH);

} // namespace deskhub
