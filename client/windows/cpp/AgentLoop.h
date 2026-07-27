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
//   MainMenuWindow → ScreenPickerDialog → **RunAgent()**
//   RunAgent CHẶN tới khi người dùng kết thúc phiên / Ctrl+C / lỗi, rồi trả exit
//   code. Trong lúc chạy nó mở một cửa sổ quản lý phiên (ui/SessionWindow.h):
//   `sources` chỉ là danh sách BAN ĐẦU — người dùng thêm/bớt nguồn giữa phiên
//   bằng các nút Add / Stop selected trên cửa sổ đó.
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

struct AgentOptions {
    uint16_t port = 47777;
    uint32_t fps = 60;
    uint32_t bitrateMbps = 20;
    bool allowInput = true; // GD4: cho client điều khiển
};

// Một màn hình được chia sẻ. `name` là tên hiện ở danh sách phía client (UTF-8).
struct AgentSource {
    HMONITOR monitor = nullptr;
    std::string name;
};

// Điều khiển phiên đang chạy (SessionWindow hoặc HeadlessAgentControl). RunAgent gọi
// vào đây để lấy lệnh thêm/bớt nguồn + tín hiệu dừng, và đẩy ngược danh sách nguồn.
struct AgentControl;

// Chạy agent phục vụ `sources` (danh sách ban đầu — thêm/bớt giữa phiên qua `ctl`)
// tới khi người dùng kết thúc phiên / Ctrl+C / lỗi. CHẶN cho tới lúc đó.
// `ctl` do NGƯỜI GỌI sở hữu và phải sống trọn lời gọi này. Trả về exit code.
int RunAgent(std::span<const AgentSource> sources, const AgentOptions& opt, AgentControl& ctl);
