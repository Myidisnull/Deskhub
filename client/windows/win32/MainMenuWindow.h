#pragma once
// =============================================================================
// MainMenuWindow.h — màn hình chính. Điểm vào của người dùng (khôi phục từ
// ff454de~1, xem main.cpp về bối cảnh).
//
// NHIỆM VỤ
//   Cửa sổ đầu tiên hiện lên khi chạy chương trình. Từ đây rẽ thành hai vai:
//     CHIA SẺ  — hiện IP máy này, bấm nút → RunAgent() với TẤT CẢ màn hình.
//     KẾT NỐI  — gõ IP máy kia → QuerySources → SourcePickerDialog → RunViewer().
//   Kèm ô chỉnh Port/FPS/Bitrate ngay trên màn hình.
//
// VỊ TRÍ TRONG LUỒNG NGƯỜI DÙNG
//   main() → **MainMenuWindow** ─┬─ AgentLoop  (vai host, chia sẻ hết màn hình)
//                               └─ SourcePickerDialog → Viewer     (vai client)
//
// LIÊN QUAN: main.cpp (người gọi), SourcePickerDialog.h, capture/DisplayFinder.h,
//            Viewer.h, AgentLoop.h, net/NetInfo.h (danh sách IP hiển thị)
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

int RunMainMenuWindow();
