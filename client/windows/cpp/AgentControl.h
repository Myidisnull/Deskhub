#pragma once
// =============================================================================
// AgentControl.h — interface điều khiển MỘT phiên host đang chạy.
//
// VÌ SAO CÓ FILE NÀY
//   RunAgent nói chuyện với frontend qua interface trừu tượng này thay vì một lớp
//   UI cụ thể — vòng lặp host không biết gì về Win32. Bản cài đặt duy nhất hiện
//   nay là SessionWindow (client/windows/win32/SessionWindow.h); interface giữ lại
//   vì nó là đường ranh mỏng, test được, giữa vòng Recv và UI.
//
// Mọi method bị gọi từ VÒNG RECV của RunAgent — bản cài đặt phải an toàn luồng
// (SessionWindow dùng hộp thư có mutex).
//
// LIÊN QUAN: AgentLoop.h (RunAgent nhận AgentControl&),
//            client/windows/win32/SessionWindow.h
// =============================================================================
#include <cstdint>
#include <vector>

#include "AgentLoop.h"  // AgentSource
#include "SessionRow.h" // SessionSourceRow

struct AgentControl {
    virtual ~AgentControl() = default;

    // True khi frontend còn sống. False = vòng Recv rơi về hành vi cũ (hết nguồn là
    // hết phiên). SessionWindow trả theo cửa sổ; headless luôn true tới khi Stop.
    virtual bool active() const = 0;

    // Người dùng yêu cầu kết thúc phiên (Stop sharing / đóng cửa sổ).
    virtual bool stopRequested() const = 0;

    // Vòng Recv đẩy danh sách nguồn hiện tại (để hiển thị). Gọi ~1s/lần.
    virtual void SetRows(std::vector<SessionSourceRow> rows) = 0;

    // Vòng Recv rút các nguồn người dùng vừa yêu cầu THÊM (nút Add).
    virtual std::vector<AgentSource> TakeAdds() = 0;

    // Vòng Recv rút các sourceId người dùng vừa yêu cầu TẮT (nút Stop selected).
    virtual std::vector<uint8_t> TakeRemoves() = 0;

    // RunAgent báo cổng THẬT đã bind (có thể khác cổng yêu cầu do dò cổng trống).
    // Không bắt buộc override — headless dùng để hiện "Sharing on port N".
    virtual void OnBound(uint16_t /*port*/) {}

    // RunAgent báo lý do nó sắp tự thoát (không mở được cổng/GPU, không nguồn nào
    // dùng được, lỗi socket...). `reasonUtf8` là chuỗi tĩnh ngắn. Không bắt buộc
    // override — headless chuyển tiếp cho C# để UI thoát trạng thái "đang chia sẻ".
    virtual void OnFailed(const char* /*reasonUtf8*/) {}
};
