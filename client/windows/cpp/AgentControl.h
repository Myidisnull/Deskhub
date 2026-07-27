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
    //
    // Đây là đường DUY NHẤT theo chiều Recv → UI. Không có TakeAdds/TakeRemoves nữa
    // (bỏ 2026-07-27): phiên chia sẻ tất cả màn hình và danh sách chốt lúc bắt đầu,
    // nên UI không còn lệnh nào gửi ngược xuống ngoài "dừng".
    virtual void SetRows(std::vector<SessionSourceRow> rows) = 0;

    // RunAgent đã bind xong kDeskhubPort và bắt đầu nghe. Không còn tham số cổng: nó
    // là hằng số (net/UdpSocket.h), frontend tự biết. Không bắt buộc override.
    virtual void OnBound() {}

    // RunAgent báo lý do nó sắp tự thoát (không mở được cổng/GPU, không nguồn nào
    // dùng được, lỗi socket...). `reasonUtf8` là chuỗi tĩnh ngắn. Không bắt buộc
    // override — frontend dùng nó để thoát trạng thái "đang chia sẻ" kèm lý do.
    virtual void OnFailed(const char* /*reasonUtf8*/) {}
};
