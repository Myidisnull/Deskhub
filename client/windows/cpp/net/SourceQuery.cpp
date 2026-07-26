// =============================================================================
// SourceQuery.cpp — cài đặt vòng hỏi-đáp LIST_SOURCES (bản Windows).
//
// CẤU TRÚC: một vòng lặp chạy tối đa 3 giây, mỗi vòng làm hai việc
//   1. Cứ 500 ms thì phát lại LIST_SOURCES. Phát lại chứ không gửi một lần, vì gói
//      đi trên UDP và gói đầu tiên mất là chuyện hoàn toàn bình thường.
//   2. Chờ nhận tối đa 200 ms. Hết hạn thì quay lại kiểm tra tổng thời gian.
//
// VỀ HAI MỐC THỜI GIAN 200 ms VÀ 500 ms
//   Hạn nhận (200 ms) phải NGẮN HƠN nhịp phát lại (500 ms), nếu không vòng lặp sẽ
//   ngủ quên trong RecvFrom và bỏ lỡ thời điểm phát lại.
//
// TRẢ VỀ NGAY KHI CÓ CÂU TRẢ LỜI ĐẦU TIÊN
//   Không chờ hết 3 giây để gom thêm — SOURCE_LIST mang trọn danh sách trong một
//   datagram (kMaxSources = 8, tên cắt ở kMaxSourceNameBytes).
//
// KHÔNG DÙNG CHUNG SOCKET VỚI PHIÊN
//   Cổng 0 = hệ thống cấp cổng tạm, host trả lời về chính cổng nguồn đó. Lúc gọi
//   hàm này phiên còn chưa tồn tại, và máy này có thể đang tự chia sẻ trên 47777 —
//   chiếm cổng đó ở đây là tự đá vào host của chính mình.
//
// LIÊN QUAN: net/SourceQuery.h (hợp đồng gọi + ý nghĩa giá trị trả về),
//            net/Discovery.cpp (cùng hình dạng vòng lặp, cho DISCOVER)
// =============================================================================
#include "net/SourceQuery.h"

#include "deskhubp/Clock.h"

namespace {
constexpr uint64_t kQueryTimeoutUs = 3'000'000; // người dùng đang đứng chờ
constexpr uint64_t kResendUs = 500'000;
constexpr uint32_t kRecvTimeoutMs = 200; // phải ngắn hơn kResendUs
} // namespace

bool QuerySources(const NetAddr& server, std::vector<deskhub::SourceInfo>& out) {
    out.clear();

    UdpSocket sock;
    if (!sock.Open(0)) return false;
    sock.SetRecvTimeout(kRecvTimeoutMs);

    // Đệm gửi và đệm nhận TÁCH RIÊNG: gói lạc nhận vào giữa chừng mà ghi đè lên
    // query thì mọi lần phát lại sau sẽ gửi rác thay vì LIST_SOURCES.
    uint8_t query[deskhub::kMaxDatagram];
    const size_t qn = deskhub::BuildListSources(query);
    if (!qn) return false;

    uint8_t buf[deskhub::kMaxDatagram];
    const uint64_t startUs = NowUs();
    uint64_t lastSendUs = 0;
    while (NowUs() - startUs < kQueryTimeoutUs) {
        const uint64_t now = NowUs();
        if (now - lastSendUs >= kResendUs) {
            lastSendUs = now;
            sock.SendTo(server, query, qn);
        }

        NetAddr from{};
        const int n = sock.RecvFrom(buf, sizeof(buf), from);
        if (n == 0) continue; // hết timeout 200ms, quay lại kiểm tra hạn 3s
        if (n < 0) {
            // Lỗi thật chứ không phải timeout: recvfrom trả về NGAY chứ không chờ
            // 200ms, nên `continue` sẽ thành vòng xoáy bận suốt 3 giây. Bỏ cuộc
            // luôn — caller tự lùi về nguồn 0.
            return false;
        }

        // Lọc kỹ: cổng này vừa mở nên về lý thuyết chỉ có host trả lời, nhưng gói
        // lạc từ máy khác trong mạng LAN vẫn tới được. Chỉ nhận gói từ ĐÚNG host
        // vừa hỏi — thiếu phép so này thì bất kỳ máy nào trong LAN đoán được cổng
        // tạm cũng tiêm được danh sách nguồn giả — và chỉ nhận đúng SOURCE_LIST.
        if (!(from == server)) continue;
        const auto span = std::span<const uint8_t>(buf, size_t(n));
        const auto h = deskhub::ParseCommonHeader(span);
        if (!h || h->type != deskhub::MsgType::SourceList) continue;

        // Đệm tạm cỡ cố định trên stack; ParseSourceList tự kẹp theo sức chứa này.
        deskhub::SourceInfo tmp[deskhub::kMaxSources];
        const size_t cnt = deskhub::ParseSourceList(*h, deskhub::PayloadOf(span), tmp);
        for (size_t i = 0; i < cnt; ++i) out.push_back(std::move(tmp[i]));
        return true;
    }

    // Hết 3 giây mà im lặng. KHÔNG phải lỗi — host đời trước GĐ6 không biết
    // LIST_SOURCES, và caller sẽ tự lùi về nguồn 0.
    return false;
}
