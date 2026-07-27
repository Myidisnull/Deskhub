#pragma once
// =============================================================================
// Viewer.h — vai CLIENT của app Win32: mỗi nguồn một cửa sổ xem.
//
// VÌ SAO VIẾT MỚI (không khôi phục ClientLoop/Renderer cũ)
//   Bộ client cũ (ClientLoop + Renderer + InputCapture, gỡ ở M4b) tự sở hữu vòng
//   recv/decode. Từ đó pipeline client đã dời vào C API (ClientApi.cpp — thêm cả
//   chuyển nguồn). Viết lại UI mỏng trên C API
//   thì viewer chạy CHUNG một pipeline với phần còn lại của app, không nuôi
//   hai bản song song.
//
// MÔ HÌNH
//   RunViewer CHẶN như RunAgent: mở một cửa sổ cho MỖI nguồn được chọn (mỗi
//   (client, nguồn) là một phiên dh_client độc lập), bơm message tới khi người
//   dùng đóng cửa sổ cuối cùng, rồi trả về để MainMenuWindow hiện lại.
//
//   Mỗi cửa sổ CHỈ có video: một cửa sổ CON aspect-fit chiếm cả vùng client —
//   dh_client_start_hwnd render thẳng vào đó (swapchain-for-HWND, zero-copy).
//   Stats và gợi ý F9 nằm ở HEADER (thanh tiêu đề), không có thanh riêng và
//   không có nút Disconnect — giống bản macOS, đóng cửa sổ là ngắt. Input bắt
//   trên chính cửa sổ con bằng ViewerInput (Raw Input + message thường; F9 khoá
//   chuột — đường chơi game giữ nguyên từ bản cũ).
//
// CALLBACK từ thread nền của phiên (stats/size/closed) KHÔNG đụng UI trực tiếp:
// ghi vào state có mutex rồi PostMessage — thread UI mới là người vẽ.
//
// LIÊN QUAN: ../cpp/DeskhubApi.h (dh_client_*), ViewerInput.h,
//            MainMenuWindow.cpp (người gọi), SourcePickerDialog.h (chọn nguồn)
// =============================================================================
#include <string>
#include <vector>

#include "deskhub/protocol/Wire.h" // SourceInfo

// CHẶN tới khi mọi cửa sổ xem đóng. `addrUtf8` là IP trần (cổng cố định 47777).
// `sources` rỗng = host đời cũ không trả lời LIST_SOURCES -> mở một cửa sổ xem
// nguồn 0. Mọi phiên đều gửi chuột/bàn phím — không còn chế độ chỉ xem.
void RunViewer(const std::string& addrUtf8, const std::vector<deskhub::SourceInfo>& sources);
