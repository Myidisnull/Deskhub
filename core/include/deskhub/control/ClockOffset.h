#pragma once
// =============================================================================
// ClockOffset.h — ước lượng độ trễ một chiều host→client, phía CLIENT.
//
// NHIỆM VỤ
//   Trả lời "frame này mất bao lâu để đi từ lúc host chụp tới lúc ta hiện nó ra?"
//   khi hai máy KHÔNG có đồng hồ chung. Vào là (pts của host, đồng hồ ta lúc frame
//   tới đích); ra là một con số mili-giây dùng được để chẩn đoán.
//
// VÌ SAO KHÔNG TRỪ THẲNG HAI ĐỒNG HỒ ĐƯỢC
//   NowUs() là đồng hồ ĐƠN ĐIỆU, mốc 0 tuỳ tiện và khác nhau giữa hai máy
//   (deskhubp/Clock.h). Gọi C = đồng hồ ta − đồng hồ host, thì mọi mẫu đo được đều
//   là (C + độ trễ thật), và C có thể lớn hàng ngày. Muốn ra độ trễ thật thì phải
//   khử C.
//
// CÁCH LÀM: BỘ LỌC MIN TRÊN ĐỘ TRỄ MỘT CHIỀU
//   Với mỗi frame, mẫu thô là  raw = đồng hồ ta khi frame tới đích − pts của host
//                                  = C + (độ trễ thật của frame đó).
//   C là hằng số, nên MIN của raw trên một cửa sổ chính là C + (độ trễ NHỎ NHẤT
//   quan sát được). Lấy raw hiện tại trừ đi cái min đó thì C biến mất hoàn toàn:
//
//       Latency(frame) = raw − min(raw)  =  độ trễ thật − sàn độ trễ
//
//   Con số này KHÔNG cần đồng bộ đồng hồ, không giả định đường truyền đối xứng, và
//   luôn ≥ 0. Đây chính là phần trễ do XẾP HÀNG — thứ duy nhất ta có thể tác động.
//
// ⚠ CÁI NÓ KHÔNG BAO GỒM, VÀ VÌ SAO CHẤP NHẬN
//   Nó không thấy được SÀN: quãng nén + nửa vòng mạng + giải mã ở trường hợp tốt
//   nhất. Sàn đó về mặt toán học KHÔNG đo được nếu không có đồng hồ chung — và cách
//   cũ (chụp một mẫu duy nhất từ HELLO_ACK rồi trừ minRtt/2) không phải là đo được
//   nó, mà là ĐOÁN nó bằng đúng một mẫu, lấy từ gói ĐẦU TIÊN của phiên — gói chậm
//   nhất một cách hệ thống (phân giải ARP/ND, Wi-Fi thức khỏi power-save, firewall,
//   Tailscale còn đi DERP trước khi lên đường trực tiếp). Sai số của cách đó là một
//   hằng số không biết dấu, không biết độ lớn, và không bao giờ được sửa lại trong
//   suốt phiên.
//
//   Nên đổi lấy: một con số HỤT một sàn ĐÃ BIẾT là gì, còn hơn một con số LỆCH một
//   lượng không ai biết. Người gọi cộng lại phần sàn đo được (minRtt/2) trước khi
//   hiển thị — xem LatencyUs.
//
// VÌ SAO MIN THEO CỬA SỔ TRƯỢT, KHÔNG PHẢI MIN TỪ ĐẦU PHIÊN
//   Min-từ-đầu-phiên không bao giờ quên. Đường truyền xuống cấp giữa chừng (roam
//   Wi-Fi, VPN rơi về relay, máy cắm sạc rồi CPU đổi nhịp) thì sàn thật đã dịch lên
//   nhưng bộ lọc vẫn neo ở đường cũ, và mọi frame sau đó bị báo trễ hơn thực tế mãi
//   mãi. Hai xô luân phiên (xô hiện tại + xô liền trước, đảo mỗi kWindowUs) cho
//   phép quên trong khoảng 1–2 cửa sổ mà chỉ tốn hai số nguyên — không cần giữ lịch
//   sử mẫu.
//
// MÔ HÌNH LUỒNG
//   Thuần C++20, không thread, không đồng hồ — thời gian bơm từ ngoài như mọi lớp
//   khác trong core. Dùng trên MỘT thread (thread Decode của client, nơi frame tới
//   đích). Test tua nhanh thời gian được, không phải sleep thật.
//
// LIÊN QUAN: deskhub/session/ClientSession.h (nguồn của minRtt qua onRtt),
//            deskhub/control/LinkStats.h (bên cạnh, cùng nhóm số liệu đường truyền),
//            docs/09-diagnostics.md §End-to-end latency
// =============================================================================
#include <cstdint>

namespace deskhub {

class ClockOffset {
public:
    // Độ dài một xô. 10 giây: đủ dài để một cửa sổ gần như chắc chắn chứa ít nhất
    // một frame đi qua đường trống (kể cả khi hình đang tĩnh và host chỉ phát
    // keepalive ~2 fps → vẫn ~20 mẫu), đủ ngắn để sàn được học lại trong vòng
    // 10–20 giây sau khi đường truyền đổi.
    static constexpr uint64_t kWindowUs = 10'000'000;

    // Bơm một mẫu. `hostPtsUs` là timestamp host gắn vào frame, `localUs` là đồng hồ
    // MÁY NÀY tại thời điểm frame tới đích (đã hiện/đã giao cho tầng hiển thị).
    //
    // Gọi Ở ĐÚNG MỘT ĐIỂM trong đường dẫn và giữ nguyên điểm đó: bộ lọc đo chênh
    // lệch giữa các mẫu, nên trộn hai điểm đo khác nhau vào cùng một bộ lọc làm sàn
    // tụt xuống theo điểm sớm hơn và mọi mẫu của điểm kia bị thổi lên.
    void AddSample(uint64_t hostPtsUs, uint64_t localUs);

    // Đã có mẫu nào chưa. Chưa có thì LatencyUs vô nghĩa.
    bool ready() const {
        return haveSample_;
    }

    // Trễ của mẫu VỪA BƠM, tính bằng micro-giây. Luôn ≥ 0 (mẫu vừa bơm đã nằm trong
    // xô hiện tại nên không bao giờ thấp hơn min).
    //
    // `netFloorUs` là phần SÀN mà người gọi đo được độc lập và muốn cộng lại — trong
    // thực tế là minRtt/2, sàn của quãng mạng. Truyền 0 để lấy con số thuần "vượt
    // trên mức tốt nhất". Nó KHÔNG bị tính hai lần: min(raw) đã khử sàn đi rồi, đây
    // là cộng lại đúng phần vừa khử mà ta đo được bằng đường khác.
    int64_t LatencyUs(uint64_t netFloorUs = 0) const;

    // Sàn hiện tại (min raw của cửa sổ trượt). Chỉ để chẩn đoán/kiểm thử — con số
    // này còn chứa C nên KHÔNG có ý nghĩa vật lý khi đứng một mình.
    int64_t floorUs() const {
        return floor_;
    }

    // Quên hết. Gọi khi phiên mới bắt đầu: mẫu của phiên trước mang một C khác.
    void Reset();

private:
    void Rotate(uint64_t localUs);

    bool haveSample_ = false;
    int64_t curMin_ = 0;  // min của xô đang mở; vô nghĩa khi curEmpty_
    int64_t prevMin_ = 0; // min của xô liền trước; vô nghĩa khi !havePrev_
    // Xô hiện tại chưa có mẫu nào. Cần cờ RIÊNG chứ không dùng giá trị canh
    // (INT64_MAX): mẫu thô là hiệu hai đồng hồ khác gốc nên mọi giá trị int64 đều
    // là mẫu hợp lệ, không có số nào để dành làm "rỗng".
    bool curEmpty_ = true;
    bool havePrev_ = false;
    int64_t floor_ = 0;    // min(curMin_, prevMin_), tính lại mỗi AddSample
    int64_t lastRaw_ = 0;  // mẫu thô vừa bơm
    uint64_t windowStartUs_ = 0;
};

} // namespace deskhub
