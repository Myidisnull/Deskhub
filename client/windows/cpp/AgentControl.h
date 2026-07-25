#pragma once
// =============================================================================
// AgentControl.h — interface điều khiển MỘT phiên host đang chạy.
//
// VÌ SAO CÓ FILE NÀY
//   Trước đây RunAgent gọi thẳng vào SessionWindow (cửa sổ Win32). Để cùng một vòng
//   lặp host phục vụ được HAI frontend — SessionWindow (client.exe cũ) và WinUI3/C#
//   (qua C API) — RunAgent giờ nói chuyện qua interface trừu tượng này thay vì một
//   lớp UI cụ thể. Ai muốn chạy phiên host thì cấp một AgentControl.
//
//   Hai bản cài đặt:
//     • SessionWindow          — Win32, dùng bởi client.exe (ui/SessionWindow.h).
//     • HeadlessAgentControl   — do C#/WinUI3 điều khiển (AgentApi.cpp).
//
// TÊN METHOD GIỮ GIỐNG HỆT SessionWindow (SetRows/TakeAdds/TakeRemoves/active/
// stopRequested) để nó chỉ cần thêm `: public AgentControl` + `override`, không phải
// đổi chữ ký. Mọi method bị gọi từ VÒNG RECV của RunAgent — bản cài đặt phải an toàn
// luồng (SessionWindow đã dùng hộp thư có mutex; headless cũng vậy).
//
// LIÊN QUAN: AgentLoop.h (RunAgent nhận AgentControl&), ui/SessionWindow.h,
//            AgentApi.cpp (HeadlessAgentControl + C API)
// =============================================================================
#include <cstdint>
#include <vector>

#include "AgentLoop.h"     // AgentSource
#include "ui/SessionRow.h" // SessionSourceRow

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
};
