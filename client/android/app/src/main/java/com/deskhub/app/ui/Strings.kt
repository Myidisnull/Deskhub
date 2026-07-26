// =============================================================================
// Strings.kt — bảng chữ EN/VI. Cùng nguồn với client/ios/app/swift/Strings.swift,
//              client/macos/app/swift/Strings.swift và Strings.cs bên Windows
//              (i18n.jsx của dự án thiết kế).
//
// VÌ SAO KHÔNG DÙNG strings.xml
//   Cơ chế resource của Android chọn ngôn ngữ theo cài đặt HỆ ĐIỀU HÀNH và đổi
//   ngôn ngữ là dựng lại Activity. Bản thiết kế đặt nút EN/VI ngay trong giao diện
//   và đổi ngay tại chỗ — nên bảng chữ phải là dữ liệu thường tra được bất cứ lúc
//   nào, không phải tài nguyên do framework nạp. strings.xml chỉ còn giữ app_name
//   (manifest đòi resource thật).
//
// CHỈ MANG PHẦN CỦA VAI CLIENT, CỘNG VÀI KHOÁ RIÊNG CỦA CẢM ỨNG — trùng từng khoá
// với bản iOS: hai client cảm ứng nói cùng một thứ tiếng.
//
// KHOÁ THIẾU THÌ TRẢ VỀ TIẾNG ANH, RỒI MỚI TỚI CHÍNH KHOÁ ĐÓ
//   Câu tiếng Anh lọt vào giao diện tiếng Việt vẫn đọc được; một chuỗi như
//   "pickTitle" hiện giữa màn hình thì không.
//
// LIÊN QUAN: AppState.kt (ngôn ngữ đang chọn)
// =============================================================================
package com.deskhub.app.ui

/**
 * Tra một khoá theo ngôn ngữ đang chọn. Gọi trong composable thì tự vẽ lại khi đổi
 * ngôn ngữ — AppState.lang là mutableStateOf và lời gọi này ĐỌC nó.
 */
fun tr(key: String): String = Strings.text(key, AppState.lang)

object Strings {
    fun text(
        key: String,
        lang: String,
    ): String {
        val table = if (lang == "vi") vi else en
        return table[key] ?: en[key] ?: key
    }

    private val en =
        mapOf(
            // Chung
            "connect" to "Connect",
            "back" to "Back",
            "language" to "Language",
            "appearanceLight" to "Light appearance",
            "appearanceDark" to "Dark appearance",
            // Connect
            "clientMode" to "Client mode",
            "connectTitle" to "Connect to a machine",
            "connectHint" to "The other machine must be sharing — Deskhub on the Mac or PC over there.",
            "addressPlaceholder" to "192.168.1.7:47777",
            "askingSources" to "asking the host what it is sharing…",
            "viewOnlyOption" to "View only — don't send touch or keyboard input",
            "recentConnections" to "Recent connections",
            "forgetAll" to "Forget all",
            "nothingRemembered" to "Machines you connect to show up here.",
            "onOtherMachine" to "On the other machine",
            "openAndShare" to "Open Deskhub and start sharing",
            "shareOrExe" to "Share this Mac · Deskhub on Windows",
            "whereAddress" to "Where the address is",
            "printedOnShare" to "It is printed on the share screen",
            "onePerInterfaceLong" to "one per interface — Wi-Fi, Ethernet, Tailscale",
            "port" to "Port",
            "udp" to "udp",
            "portChangeable" to "changeable on the share screen",
            "emulatorHint" to "Emulators reach the host machine at 10.0.2.2",
            // Pick
            "pickTitle" to "Choose what to view",
            "pickHint" to "The host is sharing these. One at a time — picking another switches the stream.",
            "sharedByHost" to "Shared by the host",
            "publishedCountFmt" to "%d sources published · %d selected",
            "startViewing" to "Start viewing",
            "noSourcesShared" to "That machine is not sharing anything right now.",
            "allowInput" to "Allow touch and keyboard",
            "unnamedSourceFmt" to "Source %d",
            // Viewer
            "streaming" to "streaming",
            "idle" to "idle",
            "connecting" to "connecting…",
            "viewOnly" to "view only",
            "end" to "End",
            "sessionEnded" to "Session ended",
            "invalidAddress" to "Invalid address",
            // Riêng cảm ứng — desktop không có bàn phím ảo, cũng không cần thanh phím tắt
            "keys" to "Keys",
            "keysOn" to "keyboard on",
            "trackpadHint" to "drag to move · tap to click · double tap for right click",
            // Xác thực (GĐ10) — trùng khoá với i18n.jsx của dự án thiết kế.
            "securityEyebrow" to "Security",
            "settingsTitle" to "Password and access",
            "connectPassword" to "Password to connect",
            "savePassword" to "Save the password for this machine",
            "passwordHintPhone" to "kept encrypted on this phone — never in a file, never synced.",
            "savedHosts" to "Saved passwords",
            "savedCountFmt" to "%d machines",
            "forget" to "Forget",
            "forgetAll" to "Forget all",
            "lastConnected" to "last connected",
            "wrongPassword" to "Wrong password",
            "lockedOut" to "Too many wrong tries — locked out, try again in a few minutes",
            "hostBusy" to "That machine is busy with another client",
            "noSavedPasswords" to "No saved passwords yet.",
        )

    private val vi =
        mapOf(
            "connect" to "Kết nối",
            "back" to "Quay lại",
            "language" to "Ngôn ngữ",
            "appearanceLight" to "Giao diện sáng",
            "appearanceDark" to "Giao diện tối",
            "clientMode" to "Chế độ client",
            "connectTitle" to "Kết nối tới một máy",
            "connectHint" to "Máy kia phải đang chia sẻ — Deskhub trên máy Mac hoặc PC bên đó.",
            "addressPlaceholder" to "192.168.1.7:47777",
            "askingSources" to "đang hỏi máy chủ xem nó chia sẻ những gì…",
            "viewOnlyOption" to "Chỉ xem — không gửi chạm và bàn phím",
            "recentConnections" to "Kết nối gần đây",
            "forgetAll" to "Xoá hết",
            "nothingRemembered" to "Máy bạn từng kết nối sẽ hiện ở đây.",
            "onOtherMachine" to "Ở máy bên kia",
            "openAndShare" to "Mở Deskhub và bắt đầu chia sẻ",
            "shareOrExe" to "Chia sẻ máy này · Deskhub trên Windows",
            "whereAddress" to "Địa chỉ lấy ở đâu",
            "printedOnShare" to "Nó hiện ngay trên màn chia sẻ",
            "onePerInterfaceLong" to "mỗi interface một địa chỉ — Wi-Fi, Ethernet, Tailscale",
            "port" to "Cổng",
            "udp" to "udp",
            "portChangeable" to "đổi được ở màn chia sẻ",
            "emulatorHint" to "Máy ảo gặp máy host ở địa chỉ 10.0.2.2",
            "pickTitle" to "Chọn thứ muốn xem",
            "pickHint" to "Máy chủ đang chia sẻ những mục này. Mỗi lần một mục — chọn mục khác sẽ chuyển luồng.",
            "sharedByHost" to "Máy chủ đang chia sẻ",
            "publishedCountFmt" to "%d nguồn được chia sẻ · đã chọn %d",
            "startViewing" to "Bắt đầu xem",
            "noSourcesShared" to "Máy đó hiện không chia sẻ gì cả.",
            "allowInput" to "Cho phép chạm và bàn phím",
            "unnamedSourceFmt" to "Nguồn %d",
            "streaming" to "đang truyền",
            "idle" to "chưa bật",
            "connecting" to "đang kết nối…",
            "viewOnly" to "chỉ xem",
            "end" to "Kết thúc",
            "sessionEnded" to "Phiên đã kết thúc",
            "invalidAddress" to "Địa chỉ không hợp lệ",
            "keys" to "Bàn phím",
            "keysOn" to "bàn phím đang bật",
            "trackpadHint" to "rê để di chuột · chạm để bấm · chạm hai lần để bấm phải",
            // Xác thực (GĐ10) — trùng khoá với i18n.jsx của dự án thiết kế.
            "securityEyebrow" to "Bảo mật",
            "settingsTitle" to "Mật khẩu và quyền truy cập",
            "connectPassword" to "Mật khẩu để kết nối",
            "savePassword" to "Lưu mật khẩu cho máy này",
            "passwordHintPhone" to "lưu đã mã hoá trên điện thoại — không ghi ra file, không đồng bộ.",
            "savedHosts" to "Mật khẩu đã lưu",
            "savedCountFmt" to "%d máy",
            "forget" to "Xoá",
            "forgetAll" to "Xoá hết",
            "lastConnected" to "kết nối lần cuối",
            "wrongPassword" to "Sai mật khẩu",
            "lockedOut" to "Sai quá nhiều lần — đang bị khoá, thử lại sau vài phút",
            "hostBusy" to "Máy đó đang bận với một client khác",
            "noSavedPasswords" to "Chưa lưu mật khẩu nào.",
        )
}
