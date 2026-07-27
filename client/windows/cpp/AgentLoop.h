#pragma once
// =============================================================================
// AgentLoop.h — vai trò HOST: giao diện gọi vào phía chia sẻ.
//
// NHIỆM VỤ
//   Khai báo đúng ba thứ: nguồn chia sẻ (AgentSource), tuỳ chọn phiên
//   (AgentOptions), và hàm chạy phiên (RunAgent). Toàn bộ phần ghép nối nằm ở
//   AgentLoop.cpp — đọc header khối ở đó để hiểu kiến trúc luồng.
//
// VỊ TRÍ TRONG KIẾN TRÚC
//   MainMenuWindow → **RunAgent()** (không có bước chọn nguồn — share hết màn hình)
//   RunAgent CHẶN tới khi người dùng kết thúc phiên / Ctrl+C / lỗi, rồi trả exit
//   code. Trong lúc chạy nó mở một cửa sổ quản lý phiên (ui/SessionWindow.h) chỉ để
//   XEM trạng thái và dừng. `sources` là danh sách CUỐI CÙNG: nó được chốt lúc bấm
//   Share (tất cả màn hình đang gắn) và không đổi nữa — nút Add / Stop selected đã
//   bỏ 2026-07-27.
//
// GĐ6: NHIỀU NGUỒN, MỘT CỔNG
//   Chia sẻ nhiều MÀN HÌNH cùng lúc trên MỘT cổng UDP (máy nhiều monitor). Mỗi
//   màn hình có sourceId riêng, và mỗi cặp (client, nguồn) là một PHIÊN ĐỘC LẬP
//   với sessionId riêng — không nhét streamId vào header video. Lý do đầy đủ ở
//   chú thích của deskhub::SourceInfo trong core/include/deskhub/protocol/Wire.h.
//
// LIÊN QUAN: AgentLoop.cpp (kiến trúc luồng + định tuyến gói), ClientLoop.h (phía
//            đối diện), capture/ScreenCapture.h, docs/06-transport.md §4
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "capture/ScreenCapture.h"

// KHÔNG có `port` và KHÔNG có `allowInput` ở đây, và đó là chủ ý (chốt 2026-07-27):
// cổng là hằng số kDeskhubPort (net/UdpSocket.h), còn chuột/bàn phím thì LUÔN được
// chia sẻ. Cả hai từng là tuỳ chọn; bỏ đi để app chỉ còn đúng một hành vi.
struct AgentOptions {
    uint32_t fps = 60;
    uint32_t bitrateMbps = 20;
};

// Một màn hình được chia sẻ. `name` là tên hiện ở danh sách phía client (UTF-8).
struct AgentSource {
    HMONITOR monitor = nullptr;
    std::string name;
};

// Điều khiển phiên đang chạy (SessionWindow — bản cài đặt duy nhất). RunAgent gọi
// vào đây để lấy tín hiệu dừng, và đẩy ngược danh sách nguồn cho UI vẽ.
struct AgentControl;

// Chạy agent phục vụ `sources` (danh sách ban đầu — thêm/bớt giữa phiên qua `ctl`)
// tới khi người dùng kết thúc phiên / Ctrl+C / lỗi. CHẶN cho tới lúc đó.
// `ctl` do NGƯỜI GỌI sở hữu và phải sống trọn lời gọi này. Trả về exit code.
int RunAgent(std::span<const AgentSource> sources, const AgentOptions& opt, AgentControl& ctl);
