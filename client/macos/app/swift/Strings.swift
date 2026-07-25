// =============================================================================
// Strings.swift — bảng chữ EN/VI, chép từ i18n.jsx của dự án thiết kế và giữ đồng bộ
//                 với client/windows/csharp/Strings.cs.
//
// VÌ SAO KHÔNG DÙNG .strings / NSLocalizedString
//   Cơ chế localization của Apple chọn ngôn ngữ theo cài đặt HỆ ĐIỀU HÀNH và cần khởi
//   động lại app để đổi. Bản thiết kế đặt nút EN/VI ngay trong giao diện và đổi ngay
//   tại chỗ — nên bảng chữ phải là dữ liệu thường tra được bất cứ lúc nào, không phải
//   tài nguyên do framework nạp.
//
// KHOÁ THIẾU THÌ TRẢ VỀ TIẾNG ANH, RỒI MỚI TỚI CHÍNH KHOÁ ĐÓ
//   Câu tiếng Anh lọt vào giao diện tiếng Việt vẫn đọc được; một chuỗi như
//   "shareTitle" hiện giữa màn hình thì không. Thấy khoá trần là biết cả hai bảng đều
//   thiếu — lỗi của người viết, không phải của người dùng.
//
// LIÊN QUAN: AppState.swift (ngôn ngữ đang chọn), i18n.jsx (nguồn gốc)
// =============================================================================
import Foundation

// Viết ngắn vì nó xuất hiện ở gần như mọi dòng chữ trong app — cùng vai trò với `t()`
// của i18n.jsx và `L.T()` của bản Windows. Không đặt tên `L` như bên C# vì SwiftLint
// (chạy --strict trong CI) bắt hàm phải bắt đầu bằng chữ thường.
@MainActor
func tr(_ key: String) -> String {
    Strings.text(key, lang: AppState.shared.lang)
}

// Chữ HOA cho eyebrow/pill. Dùng locale hiện hành chứ không phải ASCII: tiếng Việt có
// dấu, và "đang gửi".uppercased() bằng bảng ASCII sẽ giữ nguyên chữ có dấu.
@MainActor
func trU(_ key: String) -> String {
    tr(key).uppercased()
}

enum Strings {
    static func text(_ key: String, lang: String) -> String {
        let table = lang == "vi" ? vi : en
        return table[key] ?? en[key] ?? key
    }

    private static let en: [String: String] = [
        // Rail + chung
        "machines": "Machines",
        "connect": "Connect",
        "shareThisMac": "Share this Mac",
        "appearanceLight": "Light appearance",
        "appearanceDark": "Dark appearance",
        "back": "Back",
        "copy": "Copy",
        "copied": "copied",

        // Home
        "takeDesk": "Take your desk with you",
        "oneApp": "one app · host and client",
        "connectTitle": "Connect to a machine",
        "connectBody": "Enter an address, or pick a machine you have used before.",
        "takeControl": "take control →",
        "shareTitle": "Share this Mac",
        "shareBody": "Choose the windows or displays another machine may see.",
        "startSharing": "start sharing →",
        "recentConnections": "Recent connections",
        "forgetAll": "Forget all",
        "nothingRemembered": "Machines you connect to show up here.",
        "reachableFmt": "%d machines remembered",

        // Connect
        "clientMode": "Client mode",
        "connectHint": "The other machine must be sharing — Share this Mac, or Deskhub on the other side.",
        "onOtherMachine": "On the other machine",
        "openAndShare": "Open Deskhub and start sharing",
        "shareOrExe": "Share this Mac · Deskhub on Windows",
        "whereAddress": "Where the address is",
        "printedOnShare": "It is printed on the share screen",
        "onePerInterfaceLong": "one per interface — Wi-Fi, Ethernet, Tailscale",
        "port": "Port",
        "udp": "udp",
        "portChangeable": "changeable on the share screen",
        "viewOnlyOption": "View only — don't send mouse or keyboard input",
        "addressPlaceholder": "192.168.1.7:47777",
        "askingSources": "asking the host what it is sharing…",
        "connectFailed": "Could not connect — check the address and that the other machine is sharing.",

        // Pick
        "pickTitle": "Choose what to view",
        "pickHint": "The host is sharing these. One at a time — picking another switches the stream.",
        "sharedByHost": "Shared by the host",
        "publishedCountFmt": "%d sources published · %d selected",
        "startViewing": "Start viewing",
        "noSourcesShared": "That machine is not sharing anything right now.",

        // Share
        "hostMode": "Host mode",
        "privateAside": "Everything you don't tick stays private.",
        "enterOnOther": "Enter on the other machine",
        "noAddress": "No network interface found — check Wi-Fi or Ethernet.",
        "notSharingYet": "Addresses appear once you start sharing.",
        "connectedViewer": "Connected viewer",
        "noViewer": "No one is watching yet",
        "noViewerHint": "Give an address above to the other machine.",
        "viewOnly": "view only",
        "keysAndMouse": "keys + mouse",
        "sending": "sending",
        "bitrate": "bitrate",
        "capture": "capture",
        "fps": "fps",
        "windowsAndDisplays": "Windows and displays",
        "refresh": "Refresh",
        "allowInput": "Allow mouse and keyboard",
        "stop": "Stop",
        "stopAll": "Stop all",
        "share": "Share",
        "disconnect": "Disconnect",
        "starting": "starting…",
        "streaming": "streaming",
        "idle": "idle",
        "display": "display",
        "window": "window",
        "noSources": "No window or display found.",
        "sharedCountFmt": "%d of %d shared",
        "shareFailed": "Could not start sharing. The window may have closed, or the port is in use.",

        // Quyền — macOS-only, không có bên Windows
        "screenPermTitle": "Screen Recording permission required",
        "screenPermBody": "macOS needs this to capture your screen. After granting it, quit and reopen Deskhub.",
        "screenPermAction": "Open System Settings",
        "axPermTitle": "Accessibility permission required for remote control",
        "axPermBody": "Without it macOS silently drops injected input. Sharing still works in view-only mode.",
        "axPermAction": "Grant",

        // Viewer
        "mouseLocked": "Mouse locked — press F9 to release",
        "mouseFree": "Press F9 to lock the mouse",
        "end": "End",
        "connecting": "connecting…",
        "sessionEnded": "Session ended",
    ]

    private static let vi: [String: String] = [
        "machines": "Máy",
        "connect": "Kết nối",
        "shareThisMac": "Chia sẻ máy này",
        "appearanceLight": "Giao diện sáng",
        "appearanceDark": "Giao diện tối",
        "back": "Quay lại",
        "copy": "Chép",
        "copied": "đã chép",

        "takeDesk": "Mang cả desk của bạn đi khắp nơi",
        "oneApp": "một app · vừa host vừa client",
        "connectTitle": "Kết nối tới một máy",
        "connectBody": "Nhập địa chỉ, hoặc chọn máy bạn đã dùng trước đó.",
        "takeControl": "điều khiển →",
        "shareTitle": "Chia sẻ máy Mac này",
        "shareBody": "Chọn cửa sổ hoặc màn hình mà máy khác được xem.",
        "startSharing": "bắt đầu chia sẻ →",
        "recentConnections": "Kết nối gần đây",
        "forgetAll": "Xoá hết",
        "nothingRemembered": "Máy bạn từng kết nối sẽ hiện ở đây.",
        "reachableFmt": "đã nhớ %d máy",

        "clientMode": "Chế độ client",
        "connectHint": "Máy kia phải đang chia sẻ — Chia sẻ máy này, hoặc Deskhub trên Windows.",
        "onOtherMachine": "Ở máy bên kia",
        "openAndShare": "Mở Deskhub và bắt đầu chia sẻ",
        "shareOrExe": "Chia sẻ máy này · Deskhub trên Windows",
        "whereAddress": "Địa chỉ lấy ở đâu",
        "printedOnShare": "Nó hiện ngay trên màn chia sẻ",
        "onePerInterfaceLong": "mỗi interface một địa chỉ — Wi-Fi, Ethernet, Tailscale",
        "port": "Cổng",
        "udp": "udp",
        "portChangeable": "đổi được ở màn chia sẻ",
        "viewOnlyOption": "Chỉ xem — không gửi chuột và bàn phím",
        "addressPlaceholder": "192.168.1.7:47777",
        "askingSources": "đang hỏi máy chủ xem nó chia sẻ những gì…",
        "connectFailed": "Không kết nối được — kiểm tra địa chỉ và xem máy kia đã chia sẻ chưa.",

        "pickTitle": "Chọn thứ muốn xem",
        "pickHint": "Máy chủ đang chia sẻ những mục này. Mỗi lần một mục — chọn mục khác sẽ chuyển luồng.",
        "sharedByHost": "Máy chủ đang chia sẻ",
        "publishedCountFmt": "%d nguồn được chia sẻ · đã chọn %d",
        "startViewing": "Bắt đầu xem",
        "noSourcesShared": "Máy đó hiện không chia sẻ gì cả.",

        "hostMode": "Chế độ host",
        "privateAside": "Thứ bạn không tick sẽ không ai thấy.",
        "enterOnOther": "Nhập ở máy bên kia",
        "noAddress": "Không thấy card mạng nào — kiểm tra Wi-Fi hoặc Ethernet.",
        "notSharingYet": "Địa chỉ sẽ hiện ra khi bạn bắt đầu chia sẻ.",
        "connectedViewer": "Máy đang xem",
        "noViewer": "Chưa có ai xem",
        "noViewerHint": "Đọc một địa chỉ ở trên cho máy bên kia.",
        "viewOnly": "chỉ xem",
        "keysAndMouse": "chuột + bàn phím",
        "sending": "đang gửi",
        "bitrate": "bitrate",
        "capture": "thu hình",
        "fps": "fps",
        "windowsAndDisplays": "Cửa sổ và màn hình",
        "refresh": "Làm mới",
        "allowInput": "Cho phép chuột và bàn phím",
        "stop": "Dừng",
        "stopAll": "Dừng hết",
        "share": "Chia sẻ",
        "disconnect": "Ngắt",
        "starting": "đang khởi động…",
        "streaming": "đang truyền",
        "idle": "chưa bật",
        "display": "màn hình",
        "window": "cửa sổ",
        "noSources": "Không thấy cửa sổ hay màn hình nào.",
        "sharedCountFmt": "đang chia sẻ %d/%d",
        "shareFailed": "Không bắt đầu chia sẻ được — cửa sổ có thể đã đóng, hoặc cổng đang bận.",

        "screenPermTitle": "Cần quyền Screen Recording",
        "screenPermBody": "macOS cần quyền này để thu màn hình. Cấp xong hãy thoát và mở lại Deskhub.",
        "screenPermAction": "Mở System Settings",
        "axPermTitle": "Cần quyền Accessibility để điều khiển từ xa",
        "axPermBody": "Thiếu nó, macOS âm thầm bỏ mọi input được bơm vào. Chia sẻ vẫn chạy ở chế độ chỉ xem.",
        "axPermAction": "Cấp quyền",

        "mouseLocked": "Đã khoá chuột — bấm F9 để thả",
        "mouseFree": "Bấm F9 để khoá chuột",
        "end": "Kết thúc",
        "connecting": "đang kết nối…",
        "sessionEnded": "Phiên đã kết thúc",
    ]
}
