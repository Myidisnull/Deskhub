// =============================================================================
// Strings.swift — bảng chữ EN/VI. Cùng nguồn với client/macos/app/swift/Strings.swift
//                 và client/windows/csharp/Strings.cs (i18n.jsx của dự án thiết kế).
//
// CHỈ MANG PHẦN CỦA VAI CLIENT, CỘNG VÀI KHOÁ RIÊNG CỦA CẢM ỨNG
//   iOS là client-only, nên toàn bộ nhóm chữ của màn chia sẻ và của quyền hệ thống
//   (host mode, screenPerm*, axPerm*…) không có ở đây — chép sang chỉ để đó là nuôi
//   một bản dịch không ai đọc. Chiều ngược lại có bốn khoá mà desktop không cần:
//   thanh phím tắt và bàn phím ảo chỉ tồn tại trên cảm ứng.
//
// KHOÁ THIẾU THÌ TRẢ VỀ TIẾNG ANH, RỒI MỚI TỚI CHÍNH KHOÁ ĐÓ
//   Câu tiếng Anh lọt vào giao diện tiếng Việt vẫn đọc được; một chuỗi như
//   "pickTitle" hiện giữa màn hình thì không.
//
// LIÊN QUAN: AppState.swift (ngôn ngữ đang chọn)
// =============================================================================
import Foundation

// Viết ngắn vì nó xuất hiện ở gần như mọi dòng chữ trong app — cùng vai trò với `t()`
// của i18n.jsx và `L.T()` của bản Windows.
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
        // Chung
        "connect": "Connect",
        "back": "Back",
        "language": "Language",
        "appearanceLight": "Light appearance",
        "appearanceDark": "Dark appearance",

        // Connect
        "clientMode": "Client mode",
        "connectTitle": "Connect to a machine",
        "connectHint": "The other machine must be sharing — Deskhub on the Mac or PC over there.",
        "addressPlaceholder": "192.168.1.7:47777",
        "askingSources": "asking the host what it is sharing…",
        "viewOnlyOption": "View only — don't send touch or keyboard input",
        "recentConnections": "Recent connections",
        "forgetAll": "Forget all",
        "nothingRemembered": "Machines you connect to show up here.",
        "onOtherMachine": "On the other machine",
        "openAndShare": "Open Deskhub and start sharing",
        "shareOrExe": "Share this Mac · Deskhub on Windows",
        "whereAddress": "Where the address is",
        "printedOnShare": "It is printed on the share screen",
        "onePerInterfaceLong": "one per interface — Wi-Fi, Ethernet, Tailscale",
        "port": "Port",
        "udp": "udp",
        "portChangeable": "changeable on the share screen",

        // Pick
        "pickTitle": "Choose what to view",
        "pickHint": "The host is sharing these. One at a time — picking another switches the stream.",
        "sharedByHost": "Shared by the host",
        "publishedCountFmt": "%d sources published · %d selected",
        "startViewing": "Start viewing",
        "noSourcesShared": "That machine is not sharing anything right now.",
        "allowInput": "Allow touch and keyboard",
        // Host cắt tên ở 64 byte, và cửa sổ không tiêu đề thì trả về tên rỗng.
        "unnamedSourceFmt": "Source %d",

        // Viewer
        "streaming": "streaming",
        "idle": "idle",
        "connecting": "connecting…",
        "viewOnly": "view only",
        "end": "End",
        "sessionEnded": "Session ended",
        "invalidAddress": "Invalid address",

        // Riêng cảm ứng — desktop không có bàn phím ảo, cũng không cần thanh phím tắt
        "keys": "Keys",
        "keysOn": "keyboard on",
        "trackpadHint": "drag to move · tap to click · double tap for right click",
        // Xác thực (GĐ10) — trùng khoá với i18n.jsx của dự án thiết kế.
        "securityEyebrow": "Security",
        "settingsTitle": "Password and access",
        "connectPassword": "Password to connect",
        "savePassword": "Save the password for this machine",
        "biometricUnlock": "Unlock with Face ID before connecting",
        "passwordHintPhone": "kept in the keychain on this phone — never in a file, never synced.",
        "savedHosts": "Saved passwords",
        "savedCountFmt": "%d machines",
        "forget": "Forget",
        "forgetAll": "Forget all",
        "lastConnected": "last connected",
        "wrongPassword": "Wrong password",
        "lockedOut": "Too many wrong tries — locked out, try again in a few minutes",
        "hostBusy": "That machine is busy with another client",
        "noSavedPasswords": "No saved passwords yet.",
    ]

    private static let vi: [String: String] = [
        "connect": "Kết nối",
        "back": "Quay lại",
        "language": "Ngôn ngữ",
        "appearanceLight": "Giao diện sáng",
        "appearanceDark": "Giao diện tối",

        "clientMode": "Chế độ client",
        "connectTitle": "Kết nối tới một máy",
        "connectHint": "Máy kia phải đang chia sẻ — Deskhub trên máy Mac hoặc PC bên đó.",
        "addressPlaceholder": "192.168.1.7:47777",
        "askingSources": "đang hỏi máy chủ xem nó chia sẻ những gì…",
        "viewOnlyOption": "Chỉ xem — không gửi chạm và bàn phím",
        "recentConnections": "Kết nối gần đây",
        "forgetAll": "Xoá hết",
        "nothingRemembered": "Máy bạn từng kết nối sẽ hiện ở đây.",
        "onOtherMachine": "Ở máy bên kia",
        "openAndShare": "Mở Deskhub và bắt đầu chia sẻ",
        "shareOrExe": "Chia sẻ máy này · Deskhub trên Windows",
        "whereAddress": "Địa chỉ lấy ở đâu",
        "printedOnShare": "Nó hiện ngay trên màn chia sẻ",
        "onePerInterfaceLong": "mỗi interface một địa chỉ — Wi-Fi, Ethernet, Tailscale",
        "port": "Cổng",
        "udp": "udp",
        "portChangeable": "đổi được ở màn chia sẻ",

        "pickTitle": "Chọn thứ muốn xem",
        "pickHint": "Máy chủ đang chia sẻ những mục này. Mỗi lần một mục — chọn mục khác sẽ chuyển luồng.",
        "sharedByHost": "Máy chủ đang chia sẻ",
        "publishedCountFmt": "%d nguồn được chia sẻ · đã chọn %d",
        "startViewing": "Bắt đầu xem",
        "noSourcesShared": "Máy đó hiện không chia sẻ gì cả.",
        "allowInput": "Cho phép chạm và bàn phím",
        "unnamedSourceFmt": "Nguồn %d",

        "streaming": "đang truyền",
        "idle": "chưa bật",
        "connecting": "đang kết nối…",
        "viewOnly": "chỉ xem",
        "end": "Kết thúc",
        "sessionEnded": "Phiên đã kết thúc",
        "invalidAddress": "Địa chỉ không hợp lệ",

        "keys": "Bàn phím",
        "keysOn": "bàn phím đang bật",
        "trackpadHint": "rê để di chuột · chạm để bấm · chạm hai lần để bấm phải",
        // Xác thực (GĐ10) — trùng khoá với i18n.jsx của dự án thiết kế.
        "securityEyebrow": "Bảo mật",
        "settingsTitle": "Mật khẩu và quyền truy cập",
        "connectPassword": "Mật khẩu để kết nối",
        "savePassword": "Lưu mật khẩu cho máy này",
        "biometricUnlock": "Mở bằng Face ID trước khi kết nối",
        "passwordHintPhone": "lưu trong keychain của điện thoại — không ghi ra file, không đồng bộ.",
        "savedHosts": "Mật khẩu đã lưu",
        "savedCountFmt": "%d máy",
        "forget": "Xoá",
        "forgetAll": "Xoá hết",
        "lastConnected": "kết nối lần cuối",
        "wrongPassword": "Sai mật khẩu",
        "lockedOut": "Sai quá nhiều lần — đang bị khoá, thử lại sau vài phút",
        "hostBusy": "Máy đó đang bận với một client khác",
        "noSavedPasswords": "Chưa lưu mật khẩu nào.",
    ]
}
