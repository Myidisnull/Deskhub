using System.Collections.Generic;

namespace Deskhub;

// =============================================================================
// Strings — bảng chữ EN/VI, chép từ i18n.jsx của dự án thiết kế.
//
// VÌ SAO KHÔNG DÙNG .resw / ResourceLoader
//   Cơ chế tài nguyên của Windows chọn ngôn ngữ theo cài đặt HỆ ĐIỀU HÀNH và cần
//   khởi động lại app để đổi. Bản thiết kế đặt nút EN/VI ngay trong giao diện và
//   đổi ngay tại chỗ — nên bảng chữ phải là dữ liệu thường mà ta tra được bất cứ lúc
//   nào, không phải tài nguyên do framework nạp.
//
// KHOÁ THIẾU THÌ TRẢ VỀ TIẾNG ANH, RỒI MỚI TỚI CHÍNH KHOÁ ĐÓ
//   Câu tiếng Anh lọt vào giao diện tiếng Việt vẫn đọc được; một chuỗi như
//   "shareTitle" hiện giữa màn hình thì không. Thấy khoá trần là biết cả hai bảng đều
//   thiếu — đó là lỗi của người viết, không phải của người dùng.
//
// LIÊN QUAN: AppState.cs (Lang), i18n.jsx của dự án thiết kế (nguồn gốc)
// =============================================================================
public static class L
{
    public static string T(string key)
    {
        var table = AppState.Lang == "vi" ? Vi : En;
        if (table.TryGetValue(key, out var s)) return s;
        return En.TryGetValue(key, out var e) ? e : key;
    }

    private static readonly Dictionary<string, string> En = new()
    {
        // Rail + chung
        ["machines"] = "Machines",
        ["connect"] = "Connect",
        ["shareThisMac"] = "Share this PC",
        ["appearanceLight"] = "Light appearance",
        ["appearanceDark"] = "Dark appearance",

        // Home
        ["takeDesk"] = "Take your desk with you",
        ["oneApp"] = "one app · host and client",
        ["connectTitle"] = "Connect to a machine",
        ["connectBody"] = "Enter an address, or pick a machine you have used before.",
        ["takeControl"] = "take control →",
        ["shareTitle"] = "Share this PC",
        ["shareBody"] = "Choose the windows or displays another machine may see.",
        ["startSharing"] = "start sharing →",
        ["recentConnections"] = "Recent connections",
        ["forgetAll"] = "Forget all",
        ["foundOnNetworks"] = "Found on your networks",
        ["scanning"] = "scanning wi-fi · ethernet · tailscale",
        ["rescan"] = "Rescan",
        ["noneFound"] = "no machines found yet",
        ["nothingRemembered"] = "Machines you connect to show up here.",

        // Connect
        ["clientMode"] = "Client mode",

        // Pick — màn 03: chọn nguồn nào của host để xem
        ["pickTitle"] = "Choose what to view",
        ["pickHint"] = "The host is sharing these. Tick more than one to watch them side by side.",
        ["sharedByHost"] = "Shared by the host",
        ["publishedCountFmt"] = "{0} sources published · {1} selected",
        ["startViewing"] = "Start viewing",
        ["back"] = "Back",
        ["askingSources"] = "asking the host what it is sharing…",
        ["noSourcesShared"] = "That machine is not sharing anything right now.",
        ["connectHint"] = "The other machine must be sharing — Share this PC, or Deskhub on the other side.",
        ["onOtherMachine"] = "On the other machine",
        ["openAndShare"] = "Open Deskhub and start sharing",
        ["shareOrExe"] = "Share this PC · Deskhub on Windows",
        ["whereAddress"] = "Where the address is",
        ["printedOnShare"] = "It is printed on the share screen",
        ["onePerInterfaceLong"] = "one per interface — Wi-Fi, Ethernet, Tailscale",
        ["port"] = "Port",
        ["udp"] = "udp",
        ["portChangeable"] = "changeable on the share screen",
        ["viewOnlyOption"] = "View only — don't send mouse or keyboard input",
        ["addressPlaceholder"] = "192.168.1.7:47777",

        // Share
        ["hostMode"] = "Host mode",
        ["privateAside"] = "Everything you don't tick stays private.",
        ["enterOnOther"] = "Enter on the other machine",
        ["connectedViewer"] = "Connected viewer",
        ["noViewer"] = "No one is watching yet",
        ["noViewerHint"] = "Give an address above to the other machine.",
        ["viewOnly"] = "view only",
        ["keysAndMouse"] = "keys + mouse",
        ["sending"] = "sending",
        ["bitrate"] = "bitrate",
        ["roundTrip"] = "round trip",
        ["fps"] = "fps",
        ["windowsAndDisplays"] = "Windows and displays",
        ["refresh"] = "Refresh",
        ["copy"] = "Copy",
        ["allowInput"] = "Allow mouse and keyboard",
        ["shareClipboard"] = "Share clipboard",
        ["stop"] = "Stop",
        ["stopAll"] = "Stop all",
        ["share"] = "Share",
        ["disconnect"] = "Disconnect",
        ["starting"] = "starting…",
        ["streaming"] = "streaming",
        ["idle"] = "idle",
        ["display"] = "display",
        ["window"] = "window",
        ["noSources"] = "No window or display found.",
        ["sharedCountFmt"] = "{0} of {1} shared",
        ["reachableFmt"] = "{0} machines reachable · {1} found on this network",

        // Viewer
        ["mouseLocked"] = "Mouse locked — press F9 to release",
        ["mouseFree"] = "Press F9 to lock the mouse",
        ["viewOnlySession"] = "view only — this host does not accept input",
        ["end"] = "End",
        ["connecting"] = "connecting…",

        // Lỗi
        ["dllMissing"] = "deskhub_native.dll not found — build the cpp target first.",
        ["connectFailed"] = "Could not connect — check the address and that the other machine is sharing.",
        ["shareFailed"] = "Could not start sharing (see the log).",
        ["shareStoppedFmt"] = "Sharing stopped: {0}.",
    };

    private static readonly Dictionary<string, string> Vi = new()
    {
        ["machines"] = "Máy",
        ["connect"] = "Kết nối",
        ["shareThisMac"] = "Chia sẻ máy này",
        ["appearanceLight"] = "Giao diện sáng",
        ["appearanceDark"] = "Giao diện tối",

        ["takeDesk"] = "Mang cả desk của bạn đi khắp nơi",
        ["oneApp"] = "một app · vừa host vừa client",
        ["connectTitle"] = "Kết nối tới một máy",
        ["connectBody"] = "Nhập địa chỉ, hoặc chọn máy bạn đã dùng trước đó.",
        ["takeControl"] = "điều khiển →",
        ["shareTitle"] = "Chia sẻ máy này",
        ["shareBody"] = "Chọn cửa sổ hoặc màn hình mà máy khác được xem.",
        ["startSharing"] = "bắt đầu chia sẻ →",
        ["recentConnections"] = "Kết nối gần đây",
        ["forgetAll"] = "Xoá hết",
        ["foundOnNetworks"] = "Tìm thấy trong mạng của bạn",
        ["scanning"] = "đang quét wi-fi · ethernet · tailscale",
        ["rescan"] = "Quét lại",
        ["noneFound"] = "chưa tìm thấy máy nào",
        ["nothingRemembered"] = "Máy bạn từng kết nối sẽ hiện ở đây.",

        ["clientMode"] = "Chế độ client",

        // Pick — màn 03
        ["pickTitle"] = "Chọn thứ muốn xem",
        ["pickHint"] = "Máy chủ đang chia sẻ những mục này. Tick nhiều mục để xem song song.",
        ["sharedByHost"] = "Máy chủ đang chia sẻ",
        ["publishedCountFmt"] = "{0} nguồn được chia sẻ · đã chọn {1}",
        ["startViewing"] = "Bắt đầu xem",
        ["back"] = "Quay lại",
        ["askingSources"] = "đang hỏi máy chủ xem nó chia sẻ những gì…",
        ["noSourcesShared"] = "Máy đó hiện không chia sẻ gì cả.",
        ["connectHint"] = "Máy kia phải đang chia sẻ — Chia sẻ máy này, hoặc Deskhub ở đầu bên kia.",
        ["onOtherMachine"] = "Ở máy bên kia",
        ["openAndShare"] = "Mở Deskhub và bắt đầu chia sẻ",
        ["shareOrExe"] = "Chia sẻ máy này · Deskhub trên Windows",
        ["whereAddress"] = "Địa chỉ lấy ở đâu",
        ["printedOnShare"] = "Nó hiện ngay trên màn chia sẻ",
        ["onePerInterfaceLong"] = "mỗi interface một địa chỉ — Wi-Fi, Ethernet, Tailscale",
        ["port"] = "Cổng",
        ["udp"] = "udp",
        ["portChangeable"] = "đổi được ở màn chia sẻ",
        ["viewOnlyOption"] = "Chỉ xem — không gửi chuột và bàn phím",
        ["addressPlaceholder"] = "192.168.1.7:47777",

        ["hostMode"] = "Chế độ host",
        ["privateAside"] = "Thứ bạn không tick sẽ không ai thấy.",
        ["enterOnOther"] = "Nhập ở máy bên kia",
        ["connectedViewer"] = "Máy đang xem",
        ["noViewer"] = "Chưa có ai xem",
        ["noViewerHint"] = "Đọc một địa chỉ ở trên cho máy bên kia.",
        ["viewOnly"] = "chỉ xem",
        ["keysAndMouse"] = "chuột + bàn phím",
        ["sending"] = "đang gửi",
        ["bitrate"] = "bitrate",
        ["roundTrip"] = "khứ hồi",
        ["fps"] = "fps",
        ["windowsAndDisplays"] = "Cửa sổ và màn hình",
        ["refresh"] = "Làm mới",
        ["copy"] = "Chép",
        ["allowInput"] = "Cho phép chuột và bàn phím",
        ["shareClipboard"] = "Đồng bộ clipboard",
        ["stop"] = "Dừng",
        ["stopAll"] = "Dừng hết",
        ["share"] = "Chia sẻ",
        ["disconnect"] = "Ngắt",
        ["starting"] = "đang khởi động…",
        ["streaming"] = "đang truyền",
        ["idle"] = "chưa bật",
        ["display"] = "màn hình",
        ["window"] = "cửa sổ",
        ["noSources"] = "Không thấy cửa sổ hay màn hình nào.",
        ["sharedCountFmt"] = "đang chia sẻ {0}/{1}",
        ["reachableFmt"] = "{0} máy kết nối được · {1} máy tìm thấy trong mạng",

        ["mouseLocked"] = "Đã khoá chuột — bấm F9 để thả",
        ["mouseFree"] = "Bấm F9 để khoá chuột",
        ["viewOnlySession"] = "chỉ xem — máy kia không nhận điều khiển",
        ["end"] = "Kết thúc",
        ["connecting"] = "đang kết nối…",

        ["dllMissing"] = "Không thấy deskhub_native.dll — hãy dựng phần cpp trước.",
        ["connectFailed"] = "Không kết nối được — kiểm tra địa chỉ và xem máy kia đã chia sẻ chưa.",
        ["shareFailed"] = "Không bắt đầu chia sẻ được (xem log).",
        ["shareStoppedFmt"] = "Phiên chia sẻ đã dừng: {0}.",
    };
}
