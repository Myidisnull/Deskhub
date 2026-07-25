#pragma once
// =============================================================================
// DisplayFinder.h — liệt kê màn hình có thể chọn làm nguồn stream.
//
// NHIỆM VỤ
//   Đối xứng với WindowFinder: nó trả về CỬA SỔ, cái này trả về MÀN HÌNH. Màn hình
//   chia sẻ được từ GĐ6 (WindowCapture nhận CaptureTarget::Monitor), nhưng cho tới
//   nay chưa có đường nào cho giao diện HỎI xem máy này có những màn hình nào —
//   danh sách nguồn phía C# vì thế chỉ hiện được cửa sổ.
//
// VÌ SAO MÀN HÌNH VÀ CỬA SỔ KHÔNG GỘP CHUNG MỘT DANH SÁCH Ở TẦNG NÀY
//   Chúng được nhận diện bằng hai handle khác nhau (HMONITOR / HWND) và có hệ quả
//   riêng tư khác hẳn nhau, nên `deskhub::SourceKind` phân biệt chúng trên dây.
//   Gộp ở đây rồi lại tách ở tầng trên chỉ thêm một chỗ để nhầm.
//
// TÊN HIỂN THỊ
//   Windows không cho tên thương mại của màn hình qua API rẻ tiền nào (EDID phải
//   đọc registry theo instance path). Ta dùng thứ người dùng thật sự phân biệt
//   được: "Display 1 (chính)" kèm độ phân giải, theo đúng thứ tự Windows đánh số.
//
// LIÊN QUAN: capture/WindowFinder.h (bản song song cho cửa sổ),
//            capture/WindowCapture.h (CaptureTarget::Monitor), DeskhubApi.h
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
