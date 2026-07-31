#pragma once
// =============================================================================
// VideoContract.h — HỢP ĐỒNG chữ ký của bộ nén và bộ giải nén, ép ở thời điểm BIÊN DỊCH.
//
// NHIỆM VỤ
//   Năm nền có năm bộ nén/giải nén khác nhau (Media Foundation, NVENC,
//   VideoToolbox, VA-API, MediaCodec, libavcodec). Chúng KHÔNG có lớp cha chung —
//   và không cần, vì mỗi nền chỉ biên dịch đúng một backend. Cái chúng cần là một
//   QUY CHUẨN: cùng tên hàm, cùng thứ tự tham số, cùng ý nghĩa giá trị trả về, để
//   AgentLoop/ClientLoop của năm nền đọc như nhau và port qua lại chỉ là chép.
//
//   File này biến quy chuẩn đó từ lời hứa trong tài liệu thành thứ trình dịch ép
//   được. Mỗi client đặt một static_assert cạnh lớp của mình; lệch chữ ký là GÃY
//   BUILD ngay tại chỗ, không phải một lỗi chạy trên máy người dùng ba tuần sau.
//
// ⚠ VÌ SAO LÀ CONCEPT CHỨ KHÔNG PHẢI LỚP CƠ SỞ ẢO
//   Đa hình LÚC CHẠY chỉ cần ở đúng một chỗ trong toàn dự án: Windows chọn giữa
//   NVENC và Media Foundation tuỳ máy. Chỗ đó đã có IVideoEncoder/IVideoDecoder và
//   giữ nguyên. Bốn nền còn lại mỗi nền một backend duy nhất — nhét chúng vào
//   vtable chung sẽ đòi gói handle của frame (ID3D11Texture2D*, CVPixelBufferRef,
//   LinuxFrameInfo với dma-buf/planes/modifier) vào một kiểu opaque, tức là VỨT BỎ
//   kiểu mạnh của cấu trúc giàu thông tin nhất trong ba nền, và thêm một tầng ép
//   kiểu trên đúng luồng nóng — đổi lấy một khả năng không ai dùng.
//
//   Concept cho đúng thứ ta muốn (chữ ký thống nhất) mà không lấy gì của ta.
//
// KIỂU FRAME LÀ THAM SỐ, KHÔNG PHẢI THỨ ĐƯỢC THỐNG NHẤT
//   Handle của frame là biên giới nền tảng THẬT. Hợp đồng nhận nó làm tham số
//   template và chỉ ép phần còn lại — vị trí, thứ tự, kiểu trả về.
//
// ⚠ VÌ SAO ÉP BẰNG CON TRỎ HÀM THÀNH VIÊN CHỨ KHÔNG PHẢI `{ e.Encode(...) }`
//   Bản đầu của file này viết theo kiểu requires-expression thông thường:
//     { e.Encode(f, timestampUs, forceKeyframe) } -> std::same_as<bool>;
//   Nó KHÔNG bắt được lỗi. `bool` và các kiểu số tự ép qua lại, nên một cài đặt
//   viết nhầm thành Encode(frame, forceKeyframe, timestampUs) vẫn thoả — mốc thời
//   gian đi vào chỗ của cờ IDR và ngược lại, mà trình dịch im lặng. Ca này bị
//   core/tests/media/ContractTests.cpp bắt tại chỗ khi hợp đồng vừa được viết ra.
//
//   static_cast tới một kiểu con trỏ hàm thành viên CỤ THỂ thì đòi khớp CHÍNH XÁC:
//   đúng thứ tự, đúng kiểu từng tham số, đúng kiểu trả về, đúng const. Không có
//   ép kiểu ngầm nào chen vào được.
//
// TRUNG THỰC VỀ CHỖ CÒN LỆCH
//   Hợp đồng BẮT BUỘC ở dưới chỉ gồm những gì cả năm nền THẬT SỰ có. Phần nền này
//   có mà nền kia không được tách thành concept RIÊNG, có tên, thay vì giả vờ là
//   tất cả đều giống nhau:
//     - SetFps: Windows và macOS chỉnh nóng được; VA-API thì KHÔNG (fps nằm trong
//       VUI time_scale của SPS, đổi nó là phải phát IDR mới), nên Ubuntu dựng lại
//       encoder thay vì gọi hàm này — xem AgentLoop.cpp của Ubuntu.
//     - Shutdown/IsOpen: bốn viewer có; IVideoDecoder của Windows dựng lại decoder
//       bằng cách tạo đối tượng mới nên không cần.
//     - TakeCongestionDrops: chỉ có ở nền mà tầng hiển thị BẤT ĐỒNG BỘ (Apple,
//       Android) — đó cũng chính là điều kiện để trường disp_drop xuất hiện trên
//       dòng log (deskhub/diag/ClientDiag.h).
//
// LIÊN QUAN: deskhub/media/VideoTypes.h (từ vựng), client/*/encode, client/*/decode
// =============================================================================
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "deskhub/media/VideoTypes.h"

namespace deskhub::media {

// -----------------------------------------------------------------------------
// Bộ nén
// -----------------------------------------------------------------------------

// Hợp đồng TỐI THIỂU của mọi bộ nén. `Frame` là kiểu handle frame của nền đó.
//
//   Encode      nén một frame; `timestampUs` là mốc CHỤP và phải đi xuyên suốt tới
//               client (nó là đầu vào của phép đo e2e). `forceKeyframe` xin IDR.
//               false = lần nén này hỏng, KHÔNG phải encoder đã chết.
//   SetBitrate  đổi ngân sách bit giữa chừng, không dựng lại encoder nên không cần
//               IDR. false = backend không đổi được, người gọi cứ chạy tiếp với
//               bitrate cũ (đây là lý do nó trả bool chứ không phải void).
//   Finish      xả nốt và đóng.
//   BackendName tên backend để in vào log — người dùng than nóng máy thì đây là
//               dòng nói cho ta biết nó đang chạy phần cứng hay phần mềm.
template <class E, class Frame>
concept VideoEncoderLike = requires {
    static_cast<bool (E::*)(Frame, uint64_t, bool)>(&E::Encode);
    static_cast<bool (E::*)(uint32_t)>(&E::SetBitrate);
    static_cast<void (E::*)()>(&E::Finish);
    static_cast<const char* (E::*)() const>(&E::BackendName);
};

// Bộ nén đổi được fps mà KHÔNG dựng lại (Windows, macOS).
//
// Vì sao phải nói cho encoder biết khi ta chỉ đơn giản là nộp ít frame hơn: fps là
// MẪU SỐ bộ điều khiển tốc độ dùng để chia ngân sách bit cho từng frame, và cũng
// là mẫu số tính cỡ VBV. Nộp 20 frame/giây mà nó vẫn tưởng 60 thì mỗi frame chỉ
// được tiêu một phần ba số bit đáng ra được tiêu — ta hạ fps để hình NÉT HƠN mà
// nhận về hình MỜ HƠN, đúng ngược mục đích của cả cái thang chất lượng.
template <class E>
concept HotFpsEncoder = requires { static_cast<bool (E::*)(uint32_t)>(&E::SetFps); };

// -----------------------------------------------------------------------------
// Bộ giải nén
// -----------------------------------------------------------------------------

// Hợp đồng TỐI THIỂU của mọi bộ giải nén.
//
//   Decode  nạp một NAL Annex-B. `ptsUs` là mốc chụp đi xuyên suốt từ host — KHÔNG
//           được thay bằng đồng hồ nội bộ, đó là thứ client dùng để chốt e2e.
//           false = giải mã hỏng; người gọi tháo và dựng lại decoder rồi xin IDR.
template <class D>
concept VideoDecoderLike = requires {
    static_cast<bool (D::*)(const uint8_t*, size_t, uint64_t)>(&D::Decode);
};

// Decoder dựng lại được tại chỗ mà không phải tạo đối tượng mới (bốn viewer).
template <class D>
concept RestartableDecoder = requires {
    static_cast<void (D::*)()>(&D::Shutdown);
    static_cast<bool (D::*)() const>(&D::IsOpen);
};

// Decoder tự đếm frame ĐÃ LÊN MÀN HÌNH và mốc thời gian của frame cuối.
//
// Số frame vẽ được phải lấy từ đây chứ không phải đếm số lần gọi Decode: ở nền có
// tầng hiển thị bất đồng bộ, Decode trả về xong không có nghĩa là hình đã lên.
template <class D>
concept RenderCountingDecoder = requires {
    static_cast<uint32_t (D::*)()>(&D::TakeRenderedCount);
    static_cast<uint64_t (D::*)() const>(&D::lastRenderedPtsUs);
};

// Decoder báo được số frame bị TẦNG HIỂN THỊ nuốt vì nghẽn (Apple, Android).
//
// Trên các nền đó đây là con số DUY NHẤT lộ ra ùn tắc — hàng đợi giải mã luôn rỗng
// vì việc đưa hình lên màn là bất đồng bộ. Nó là điều kiện để trường disp_drop có
// mặt trên dòng evt=sum (deskhub/diag/ClientDiag.h).
template <class D>
concept CongestionAwareDecoder = requires {
    static_cast<uint32_t (D::*)()>(&D::TakeCongestionDrops);
};

} // namespace deskhub::media
