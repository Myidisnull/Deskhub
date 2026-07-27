#pragma once
// =============================================================================
// ScreenPickerDialog.h — hộp thoại chọn MÀN HÌNH để chia sẻ, phía HOST.
//
// NHIỆM VỤ
//   Bước giữa "bấm nút Chia sẻ" và "bắt đầu stream": chọn màn hình nào (máy nhiều
//   monitor), và có cho điều khiển hay không. (Danh sách từng gồm cả cửa sổ —
//   share theo cửa sổ đã bỏ 2026-07-27, app tập trung thuần remote desktop.)
//
// VÌ SAO GỘP CHECKBOX "CHO ĐIỀU KHIỂN" VÀO ĐÂY
//   Đây là quyết định về QUYỀN, phải nằm ngay cạnh quyết định "chia sẻ cái gì"
//   để người dùng thấy cả hai cùng lúc.
//
// LIÊN QUAN: MainMenuWindow.h (nơi mở), capture/DisplayFinder.h, AgentLoop.h,
//            SourcePickerDialog.h (đối xứng phía client)
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vector>

#include "AgentLoop.h" // AgentSource

// Hiện hộp thoại MODAL (vô hiệu hoá `owner` trong lúc mở). Trả false nếu người
// dùng bấm Hủy/đóng cửa sổ hoặc không chọn màn hình nào. `outAllowInput` chỉ có ý
// nghĩa khi trả về true.
bool ShowScreenPickerDialog(HWND owner, std::vector<AgentSource>& outSources,
    bool& outAllowInput);

// Biến thể "thêm màn hình GIỮA PHIÊN" (nút Add của SessionWindow): cùng danh sách,
// nhưng KHÔNG có checkbox điều khiển — quyền điều khiển là quyết định một lần
// cho cả phiên, đã chốt lúc bấm Share. Nút xác nhận đề "Add" thay vì "Share".
bool ShowScreenPickerAddDialog(HWND owner, std::vector<AgentSource>& outSources);
