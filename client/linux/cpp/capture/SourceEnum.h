#pragma once
// =============================================================================
// SourceEnum.h — lấy danh sách MÀN HÌNH máy này chia sẻ được.
//                Đối ứng client/macos/.../capture/SourceEnum.h và
//                client/windows/cpp/capture/DisplayFinder.h.
//
// ⚠ NGƯỢC HƯỚNG SO VỚI WINDOWS/macOS — ĐỌC KỸ TRƯỚC KHI SỬA UI
//   Bên Windows/macOS, hàm này HỎI hệ điều hành "có những màn hình nào?" rồi app tự
//   vẽ danh sách cho người dùng tick. Trên Wayland điều đó là bất khả: không API
//   nào cho ứng dụng biết có màn hình nào để quay, chứ đừng nói tới quay.
//
//   Nên ở đây, hàm này KHÔNG liệt kê — nó CHẠY HỘP THOẠI CHỌN MÀN HÌNH của hệ
//   thống (qua PortalScreenCast) và trả về đúng những màn hình NGƯỜI DÙNG ĐÃ CHỌN.
//   Danh sách trả về vì thế là kết quả cuối cùng, không phải một menu để chọn tiếp:
//   UI chỉ hiện nó ra cho người dùng xác nhận rồi bấm Share.
//
//   Hệ quả cho UI: gọi hàm này CHÍNH LÀ hành động "bấm nút Share", không phải bước
//   chuẩn bị trước đó. Đừng gọi nó để làm mới danh sách theo nhịp — mỗi lần gọi
//   (khi chưa có phiên portal) là một lần hộp thoại hệ thống nhảy ra.
//
// ⚠ CHẶN
//   GetShareSources() chờ người dùng bấm trong hộp thoại → gọi ngoài main thread,
//   giống QuerySources của vai client.
//
// LIÊN QUAN: capture/PortalScreenCast.h (nơi làm việc thật),
//            gtk/MainWindow.cpp (người gọi), AgentLoop.h (AgentSource)
// =============================================================================
#include <cstdint>
#include <string>
#include <vector>

// Một màn hình người dùng đã đồng ý chia sẻ.
struct ShareSource {
    uint32_t nodeId = 0;  // node PipeWire
    std::string name;     // "DP-1 (2560×1440)"
    int32_t x = 0, y = 0; // vị trí trong desktop toàn cục (cho chuột tuyệt đối)
    uint32_t width = 0;   // kích thước theo portal; kích thước THẬT để encode lấy
    uint32_t height = 0;  // từ thoả thuận PipeWire (có thể lệch khi scale phân số)
};

// CHẶN cho tới khi người dùng bấm xong hộp thoại hệ thống. Trả danh sách rỗng nếu
// người dùng huỷ, máy không có xdg-desktop-portal, hoặc D-Bus lỗi — lý do cụ thể
// nằm ở ShareSourceError().
std::vector<ShareSource> GetShareSources();

// Lý do lần GetShareSources() gần nhất trả về rỗng, để UI nói được điều gì đó tử tế.
std::string ShareSourceError();

// Đóng phiên portal. Gọi khi người dùng dừng chia sẻ — không gọi thì chỉ báo "đang
// quay màn hình" của compositor còn sáng cho tới khi app thoát.
void ReleaseShareSources();
