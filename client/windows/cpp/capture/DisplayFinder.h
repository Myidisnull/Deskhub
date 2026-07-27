#pragma once
// =============================================================================
// DisplayFinder.h — liệt kê màn hình có thể chọn làm nguồn stream.
//
// NHIỆM VỤ
//   Nguồn chia sẻ duy nhất của app là màn hình (share theo cửa sổ đã bỏ
//   2026-07-27), và đây là nơi duy nhất liệt kê chúng — nuôi picker "chọn màn
//   hình để share" và multi-monitor.
//
// TÊN HIỂN THỊ
//   Windows không cho tên thương mại của màn hình qua API rẻ tiền nào (EDID phải
//   đọc registry theo instance path). Ta dùng thứ người dùng thật sự phân biệt
//   được: "Display 1 (chính)" kèm độ phân giải, theo đúng thứ tự Windows đánh số.
//
// LIÊN QUAN: capture/ScreenCapture.h (nơi nhận HMONITOR),
//            client/windows/win32/WindowPickerDialog.h (picker dùng danh sách này)
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

// Một màn hình có thể chọn làm nguồn stream.
struct DisplayInfo {
    HMONITOR monitor = nullptr;
    std::wstring name;              // "Display 1" / "Display 2 (primary)"
    uint32_t width = 0, height = 0; // kích thước theo pixel vật lý
    bool primary = false;
};

// Liệt kê màn hình đang gắn, màn hình CHÍNH luôn đứng đầu (nó là thứ người dùng
// nghĩ tới khi nói "màn hình của tôi"), phần còn lại theo thứ tự Windows trả về.
std::vector<DisplayInfo> ListDisplays();
