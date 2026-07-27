#pragma once
// =============================================================================
// SessionRow.h — một dòng trong danh sách nguồn đang chia sẻ (phía host).
//
// Là kiểu dữ liệu AgentControl::SetRows dùng để đẩy danh sách nguồn ra frontend
// (HeadlessAgentControl → C# hiện ở màn Sharing status). Trước đây dùng chung với
// lớp UI Win32 (SessionWindow/ViewerWindow, đã gỡ ở M4b); nay chỉ còn AgentControl.
// =============================================================================
#include <cstdint>
#include <string>

struct SessionSourceRow {
    uint8_t sourceId = 0;
    std::wstring label;   // "tên (WxH, ...)" / "tên (starting...)"
    bool pending = false; // true = đang chờ (frame đầu / đàm phán), chưa chạy hẳn

    // --- Các trường CÓ CẤU TRÚC (GĐ9) ---
    // `label` là một chuỗi đã ghép sẵn bằng tiếng Anh. Giao diện mới hiện được hai
    // ngôn ngữ và vẽ mỗi mẩu thông tin ở một chỗ khác nhau (tên đậm, độ phân giải
    // mono, huy hiệu trạng thái riêng) — nên nó cần từng mẩu RỜI, không phải một
    // câu. Giữ `label` lại vì nó vẫn là cách hiển thị gọn cho log và cho bản dựng cũ.
    std::wstring name; // tên màn hình, không hậu tố
    uint32_t width = 0, height = 0;
    bool viewerConnected = false;
    std::string viewerAddr; // "ip:port" của client đang xem, rỗng nếu không có
    uint32_t fps = 0;       // đang gửi, cửa sổ 1 giây gần nhất
    uint32_t kbps = 0;
    uint32_t rttMs = 0; // từ FEEDBACK của client; 0 = chưa có số

    // HMONITOR của nguồn đóng gói uint64 — khoá ổn định để giao diện khớp dòng ↔
    // mục danh sách (tên "Display 1" có thể trùng khi cắm lại màn hình).
    uint64_t monitor = 0;

    // So sánh để SetRows bỏ qua khi không đổi. Bao luôn các trường số: chúng đổi
    // mỗi giây và chính là thứ giao diện cần vẽ lại.
    bool operator==(const SessionSourceRow& o) const = default;
};
