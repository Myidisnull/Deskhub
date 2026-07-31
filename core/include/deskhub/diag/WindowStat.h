#pragma once
// =============================================================================
// WindowStat.h — bốn kiểu bộ đếm CỬA SỔ dùng chung cho mọi dòng [DIAG].
//
// NHIỆM VỤ
//   Mọi con số trong docs/09 đều là một trong bốn hình dạng:
//     - trung bình + lớn nhất trên cửa sổ  (asm_ms, dec_ms, enc_ms, enc_lat_ms…)
//     - đếm số lần trong cửa sổ            (dq_drop, disp_drop, idr, send_fail…)
//     - lớn nhất trong cửa sổ              (loop_busy_ms_max, burst_ms_max…)
//     - nhỏ nhất TỪNG THẤY, không reset    (min_rtt_ms)
//   Bốn lớp dưới đây là bốn hình dạng đó. Không có gì hơn.
//
// ⚠ VÌ SAO PHẢI GOM VỀ MỘT CHỖ
//   Trước 31/07/2026 mỗi client tự viết lại phép "cập nhật max", và chúng KHÔNG
//   giống nhau: Windows/macOS/Ubuntu/iOS dùng vòng CAS, Android dùng
//   load-rồi-store. Bản Android ĐÚNG, nhưng chỉ vì một tiền đề không ai viết ra:
//   dgDecMsMax_ khi ấy có duy nhất MỘT thread ghi. Thêm người ghi thứ hai là nó
//   sai lặng lẽ — mà phía host thì đã có sẵn ca đó (SourcePipeline::DiagEncode
//   được gọi từ cả thread capture lẫn thread Recv), nên tiền đề ấy không phải
//   thứ đem đi dùng lại được.
//
//   Add() dưới đây dùng CAS vô điều kiện: đúng với một người ghi lẫn nhiều người
//   ghi, và không còn tiền đề nào để ai đó vô tình phá. Kèm theo, năm bản chép
//   tay của cùng một dòng log đã lệch nhau về THỨ TỰ TRƯỜNG giữa Windows và
//   nhóm Apple — một bản thì không lệch được nữa.
//
// MÔ HÌNH LUỒNG: MỘT NGƯỜI ĐỌC, NHIỀU NGƯỜI GHI
//   Add() gọi từ thread ĐO (Decode, Encode, hiển thị) — nhiều thread cũng được.
//   TakeReset() gọi từ thread IN (vòng Net/Recv), ĐÚNG MỘT LẦN mỗi cửa sổ: nó
//   đọc-và-xoá, gọi hai lần liên tiếp thì lần thứ hai ra 0.
//
//   Ba trường của WindowStat được xoá bằng ba lời gọi exchange riêng, KHÔNG phải
//   một thao tác nguyên tử chung. Nghĩa là một mẫu rơi đúng vào khe giữa hai
//   exchange có thể bị tính sang cửa sổ bên cạnh. Chấp nhận có chủ ý: khoá lại
//   để đổi lấy độ chính xác đó là đặt một mutex lên luồng nóng, mà sai số ở đây
//   là một mẫu trên hàng nghìn — không đổi kết luận chẩn đoán nào.
//
// KHÔNG CÓ I/O Ở ĐÂY: core giữ nguyên tắc "core stays I/O-free" của docs/09.
// Phần dựng chuỗi nằm ở ClientDiag.h / AgentDiag.h, phần GHI nằm ở từng client.
//
// LIÊN QUAN: deskhub/diag/ClientDiag.h, deskhub/diag/AgentDiag.h,
//            docs/09-diagnostics.md
// =============================================================================
#include <atomic>
#include <cstdint>

namespace deskhub::diag {

// Trung bình + lớn nhất + số mẫu của một cửa sổ. Đơn vị do người gọi quy ước
// (mọi chỗ trong dự án đang dùng mili-giây).
class WindowStat {
public:
    struct Snapshot {
        double avg = 0.0; // 0 khi cửa sổ không có mẫu nào
        uint32_t max = 0;
        uint32_t count = 0;
    };

    // Thread ĐO. Rẻ như một fetch_add, trừ khi đang lập kỷ lục mới.
    void Add(uint32_t v) {
        sum_.fetch_add(v, std::memory_order_relaxed);
        count_.fetch_add(1, std::memory_order_relaxed);
        // Vòng CAS chuẩn: thất bại thì `cur` được nạp lại và điều kiện xét lại,
        // nên không bao giờ ghi đè một max lớn hơn bằng số nhỏ hơn.
        uint32_t cur = max_.load(std::memory_order_relaxed);
        while (v > cur && !max_.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }

    // Thread IN, một lần mỗi cửa sổ. Đọc-và-xoá.
    Snapshot TakeReset() {
        const uint32_t s = sum_.exchange(0, std::memory_order_relaxed);
        const uint32_t m = max_.exchange(0, std::memory_order_relaxed);
        const uint32_t c = count_.exchange(0, std::memory_order_relaxed);
        return Snapshot{c ? double(s) / c : 0.0, m, c};
    }

private:
    std::atomic<uint32_t> sum_{0};
    std::atomic<uint32_t> max_{0};
    std::atomic<uint32_t> count_{0};
};

// Đếm số lần xảy ra trong cửa sổ.
class WindowCount {
public:
    void Add(uint32_t n = 1) {
        n_.fetch_add(n, std::memory_order_relaxed);
    }
    uint32_t TakeReset() {
        return n_.exchange(0, std::memory_order_relaxed);
    }
    uint32_t peek() const {
        return n_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint32_t> n_{0};
};

// Giá trị lớn nhất trong cửa sổ, không cần trung bình.
class WindowMax {
public:
    void Add(uint32_t v) {
        uint32_t cur = v_.load(std::memory_order_relaxed);
        while (v > cur && !v_.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }
    uint32_t TakeReset() {
        return v_.exchange(0, std::memory_order_relaxed);
    }

private:
    std::atomic<uint32_t> v_{0};
};

// Nhỏ nhất TỪNG THẤY từ đầu phiên — KHÔNG reset theo cửa sổ.
//
// Dùng cho min_rtt_ms, và nó là đầu vào của ước lượng e2e (xem
// deskhub/control/ClockOffset.h): sàn mạng phải là con số ổn định nhất có thể,
// nên lấy kỷ lục cả phiên chứ không phải kỷ lục một giây. Giá trị 0 nghĩa là
// CHƯA CÓ MẪU NÀO, không phải "0 micro-giây" — người gọi phải phân biệt.
class RunningMin {
public:
    void Add(uint32_t v) {
        if (!v) return; // 0 là "chưa có", không phải một mẫu hợp lệ
        uint32_t cur = v_.load(std::memory_order_relaxed);
        while ((cur == 0 || v < cur) &&
               !v_.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }
    uint32_t value() const {
        return v_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint32_t> v_{0};
};

} // namespace deskhub::diag
