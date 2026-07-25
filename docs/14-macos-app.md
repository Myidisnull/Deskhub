# 14 — App macOS (SwiftUI + ScreenCaptureKit + VideoToolbox + CGEvent)

**Một app, CẢ HAI VAI** — kiểu AnyDesk, đúng như `client.exe` bên Windows
(`01-architecture.md` §1). Đây là điểm khác căn bản so với iOS/Android: hai nền tảng
đó là client-only vì sandbox chặn tuyệt đối việc bơm input vào app khác và việc mở
cổng nghe (`11-platform-transport.md` §3); macOS thì làm được cả hai, chỉ cần người
dùng cấp quyền.

**Trạng thái: 🔶 ĐÃ TRIỂN KHAI (cả agent lẫn client).** Chưa chạy kiểm chứng hai máy
thật — xem §8.

## 1. Phân chia Swift / C++

`core/` dùng lại **y nguyên** (thuần C++20, không header hệ điều hành). Phần phải
viết mới là các lớp mỏng platform-specific. Bảng dưới đọc theo cột: mỗi hàng là một
vai trò, và ô macOS là file mới trong `client/macos/app/cpp` hoặc `app/swift`.

| Vai trò | Windows | iOS | **macOS** |
|---------|---------|-----|-----------|
| datagram vào/ra | `net/UdpSocket.cpp` (winsock) | `net/UdpSocket.cpp` (BSD) | **`net/UdpSocket.cpp` — chép từ iOS** |
| LIST_SOURCES trước phiên | `SourcePickerDialog` | `net/SourceQuery.cpp` | **`net/SourceQuery.cpp` — chép từ iOS** |
| địa chỉ IPv4 của máy | `net/NetInfo.cpp` | — | **`net/NetInfo.cpp`** (getifaddrs) |
| **client** decode + render | `MfDecoder` + `Renderer` | `VtDecoder` | **`client/VtDecoder.mm` — chép từ iOS** |
| **client** vòng đời phiên | `ClientLoop.cpp` | `ClientLoop.cpp` | **`client/ClientLoop.cpp`** |
| **client** bắt input | `input/InputCapture.cpp` | `TouchInputView` + `KeyInputView` | **`swift/RemoteView.swift`** |
| **agent** capture | `capture/WindowCapture` (WGC) | — | **`agent/ScreenCapture.mm`** (SCStream) |
| **agent** encode | `encode/NvencEncoder` | — | **`agent/VtEncoder.mm`** (VTCompressionSession) |
| **agent** inject | `input/InputInjector` (SendInput) | — | **`agent/InputInjector.mm`** (CGEvent) |
| **agent** liệt kê nguồn | `capture/WindowFinder` | — | **`agent/SourceEnum.mm`** (SCShareableContent) |
| **agent** điều phối | `AgentLoop.cpp` | — | **`agent/AgentLoop.cpp`** |
| **agent** "host thắng" | `input/LocalInputMonitor` | — | **`agent/LocalInputMonitor.mm`** |
| clipboard hai chiều | `ClipboardSync.cpp` | — | **`agent/ClipboardSync.mm`** (NSPasteboard) |
| ranh giới UI ↔ C++ | — | `DeskhubClient.mm` | **`DeskhubBridge.mm`** (thêm nhóm `dha_*`) |
| bảng phím | (Raw Input cho sẵn scancode) | `core/KeyMap.h` | **`input/MacKeyMap.cpp`** |

`UdpSocket`, `SourceQuery` và `VtDecoder` **chép từ iOS gần như không sửa** — lời hứa
của `11-platform-transport.md` §5 được thu về tiền mặt lần thứ hai. Việc thật sự phải
viết cho macOS là **ba backend của vai host** và **lớp bắt input desktop**.

## 2. Vai AGENT — ba backend

### 2a. Capture: ScreenCaptureKit

`SCStream` là đối ứng chính xác của WGC: **theo sự kiện, không polling**, và **chỉ
phát frame khi nội dung ĐỔI** (`SCFrameStatusComplete` vs `Idle`). Hệ quả quan trọng:
**cơ chế cache frame cuối của `AgentLoop` vẫn cần y nguyên** — nguồn đứng im mà client
xin IDR thì không có gì để nén, không cache thì client vào xem màn hình tĩnh sẽ đen
vĩnh viễn (`02-agent.md` §7, và chú thích đầu `AgentLoop.cpp`).

Ba điểm macOS phát sinh so với WGC, ghi vào `ScreenCapture.mm`:

- **NV12 thẳng từ nguồn.** Cấu hình `pixelFormat = '420v'` nên VideoToolbox nhận đúng
  thứ nó muốn. Bản Windows phải qua video processor để đổi BGRA→NV12; ở đây khỏi.
- **Kích thước buffer CỐ ĐỊNH sau khi tạo stream.** Người dùng kéo cửa sổ to ra thì
  SCStream vẫn giao buffer cũ với nội dung bị co lại, và **không có sự kiện nào báo**.
  Nên `.mm` chạy một dispatch timer 500ms tự so cỡ nguồn với cỡ buffer và gọi
  `updateConfiguration` khi lệch; frame sau đó về đúng cỡ mới và `AgentLoop` nhận ra
  qua đúng đường `sizeChanged` như bản Windows.
- **Cửa sổ đóng cũng không có sự kiện.** SCStream chỉ ngừng phát frame, và một nguồn
  im lặng nhìn y hệt mạng hỏng. Cùng timer đó hỏi `CGWindowListCopyWindowInfo`; mảng
  rỗng = cửa sổ đã đóng → `Closed()`.

`queueDepth = 5` chứ không 3: `AgentLoop` **retain** một buffer làm cache frame cuối,
và VideoToolbox giữ thêm một cái trong lúc nén. Retain rẻ hơn nhiều so với chép 12 MB
mỗi frame ở 4K.

### 2b. Encode: VTCompressionSession

Cùng chính sách low-latency với NVENC (`02-agent.md` §3): `RealTime = true`,
`AllowFrameReordering = false` (không B-frame), **GOP vô hạn + IDR theo yêu cầu**,
`DataRateLimits` chặn burst. Không có lớp `IVideoEncoder` trừu tượng như Windows —
Windows cần nó vì có bốn backend tuỳ GPU, macOS chỉ có một đường.

**Chỗ dễ sai nhất: AVCC → Annex-B.** VideoToolbox xuất AVCC (tiền tố ĐỘ DÀI 4 byte,
SPS/PPS nằm ngoài luồng trong `CMVideoFormatDescription`); giao thức Deskhub đòi
Annex-B với **SPS/PPS đi kèm MỖI IDR** — đúng như NVENC bật `repeatSPSPPS`
(`08-android-client.md` §3). Bỏ bước chèn SPS/PPS thì client kết nối giữa chừng không
bao giờ giải mã được, vì `VtDecoder` bỏ mọi frame cho tới khi thấy tham số.

Khác bản Windows một điểm về luồng: **callback `onPacket` KHÔNG chạy trên thread gọi
`Encode()`** — VideoToolbox trả kết quả bất đồng bộ. `VtEncoder` khoá quanh phần thân
callback nên các lời gọi vẫn nối tiếp và đúng thứ tự, thứ mà `Packetizer` đòi hỏi (nó
single-thread, không tự khoá).

### 2c. Inject: CGEvent

Đối ứng `SendInput`, cùng ba cơ chế an toàn của bản Windows (`02-agent.md` §5):

1. **Chốt foreground** — CGEventPost bơm vào ứng dụng đang foreground, không vào một
   cửa sổ cụ thể. So pid của ứng dụng sở hữu cửa sổ nguồn với
   `NSWorkspace.frontmostApplication`. Nguồn là cả màn hình thì bỏ chốt.
2. **Chống kẹt phím** (`ReleaseAll`) — client mất kết nối giữa lúc giữ W.
3. **Host thắng** (`LocalInputMonitor`) — người ngồi tại máy vừa dùng chuột/phím thật
   thì input từ xa nhường 1 giây.

Hai chỗ macOS phát sinh:

- **Ánh xạ VK Windows → keycode Carbon.** Giao thức nói bằng VK + scancode PC
  (`Wire.h`). Ta ưu tiên **VK** chứ không scancode — ngược bản Windows, vì scancode là
  mã bàn phím PC còn macOS không có khái niệm tương ứng. Bảng ở `input/MacKeyMap.cpp`,
  **một bản duy nhất**, dùng cho cả chiều ngược (vai client).
- **Lọc chính input mình bơm ra.** Sự kiện CGEventPost quay trở lại qua
  `LocalInputMonitor` y như sự kiện thật; không lọc thì mỗi phím từ xa tự đánh dấu
  "người ngồi máy vừa gõ" và kênh điều khiển **tự khoá chính nó vĩnh viễn**. Ta đóng
  dấu `kCGEventSourceUserData = kUserData` vào mọi sự kiện bơm ra — đúng vai cờ
  `LLMHF_INJECTED` bên Windows, chỉ khác là phải tự đóng.

Ngoài ra `clickState` phải tự đếm: macOS **không** tự suy ra double-click, ứng dụng
đọc thẳng trường đó — không đặt thì hai cú click nhanh chỉ là hai click đơn.

## 3. Vai CLIENT

`ClientLoop` port sát bản iOS; ba thread và hai cơ chế đồng bộ (hàng đợi frame, bắt
tay layer) giữ nguyên từng dòng — xem `12-ios-client.md` §3 và header của
`client/ClientLoop.h`. `VtDecoder` chép từ iOS: `AVSampleBufferDisplayLayer` giống hệt
nhau trên hai nền tảng, kèm nguyên caveat **e2e đo lúc ENQUEUE chứ chưa phải lúc lên
màn hình** (`12-ios-client.md` §2).

Phần khác iOS nằm trọn ở **kênh input**, vì macOS là máy desktop có bàn phím và chuột
thật:

- `QueueKey(vk, scan, down)` — nhấn/nhả **riêng biệt**, không phải "tap" ghép sẵn. Giữ
  W để nhân vật chạy là điều bàn phím ảo iOS không làm được.
- **Chuột tương đối dùng thật** — khoá chuột bằng **F9** như client Windows, qua
  `CGAssociateMouseAndMouseCursorPosition(false)`. Bắt buộc cho game FPS
  (`07-phase4-input.md` §5).
- **Con lăn** và **clipboard hai chiều** (GĐ8) — hai máy desktop copy/paste qua lại.
- `ReleaseAllInput()` khi view mất focus. iOS không có khái niệm này; ở đây thiếu nó
  là kẹt phím ở máy kia cho tới khi timeout 5 giây.

`RemoteView.swift` gộp **hiển thị + bắt input vào MỘT NSView** (iOS phải tách ba
lớp). Lý do: một NSView vừa là first responder nhận trọn
`keyDown`/`mouseMoved`/`scrollWheel`, vừa là view chứa layer video — tách ra chỉ tạo
thêm ranh giới phải đồng bộ toạ độ.

## 4. UI SwiftUI

Một cửa sổ, năm màn, hai nhánh:

```
home ──► connect ──► sourcePicker ──► stream      (vai CLIENT)
     └─► share   ──► session                      (vai HOST)
```

- **`HomeView`** — chọn vai. Không nhớ lựa chọn lần trước: chọn nhầm vai trên một app
  điều khiển từ xa khó chịu hơn nhiều so với bấm thêm một nút.
- **`ConnectView` / `SourcePickerView` / `StreamView`** — đối ứng ba màn của iOS.
  `StreamView` **không có thanh phím tắt** như iOS: macOS có bàn phím thật nên
  `RemoteView` gửi thẳng Esc/Tab/F-key. Ngoại lệ duy nhất là F9 (phím thoát hiểm).
- **`ShareView`** — chọn nguồn (tick nhiều cái, GĐ6), cổng/fps/bitrate, công tắc cho
  điều khiển. Banner quyền nằm **trên cùng** và nút Start bị khoá khi thiếu Screen
  Recording — thà chặn rõ ràng còn hơn để người dùng nhận một thất bại không giải
  thích được.
- **`SessionView`** — đối ứng `ui/SessionWindow.cpp` bên Windows: hiện **địa chỉ +
  cổng** để đọc cho máy kia, số liệu sống từng nguồn, và thêm/bớt nguồn giữa phiên.

Điều phối vẫn là "không View nào gọi thẳng hàm C": mọi lối đi qua `DeskhubClient.swift`
/ `DeskhubAgent.swift`.

Khác bản Windows một điểm kiến trúc: `RunAgent()` bên Windows **chặn** tới hết phiên.
Ở đây UI là SwiftUI trên main thread nên `AgentLoop::Start()` dựng thread Recv rồi trả
về ngay, và UI hỏi trạng thái 500ms/lần.

## 5. Quyền hệ thống — đọc mục này trước khi báo lỗi

Trên macOS, thiếu quyền **không báo lỗi** mà im lặng cho ra kết quả sai. Đây là bản
macOS của bẫy UIPI bên Windows (`ElevatedShare.h`).

| Quyền | Cần cho | Thiếu thì |
|-------|---------|-----------|
| **Screen Recording** | vai host (liệt kê nguồn + bắt hình) | `SCShareableContent` chỉ trả về cửa sổ **của chính Deskhub** → danh sách nguồn gần như trống |
| **Accessibility** | bơm input + "host thắng" | `CGEventPost` chạy "thành công" nhưng **không sự kiện nào tới ứng dụng đích** |
| **Local Network** | mọi vai (macOS 15+) | gói UDP nội mạng bị chặn im lặng |

Hai điểm dễ mất thời gian:

- Screen Recording đòi **khởi động lại app** sau khi bật công tắc. Accessibility thì
  có hiệu lực ngay.
- `CGRequestScreenCaptureAccess()` chỉ bật hộp thoại **đúng một lần trong đời app**;
  lần sau nó lặng lẽ trả trạng thái hiện tại. Nên UI luôn kèm nút mở thẳng System
  Settings thay vì dựa vào hộp thoại.

App **không sandbox** (`app/Deskhub.entitlements`): Accessibility không cấp cho tiến
trình sandboxed, và vai host phải bind một cổng UDP cố định. Đây cũng là lý do các app
điều khiển từ xa đều phát hành ngoài Mac App Store.

## 6. Build & chạy

**Xcode project** như iOS, không dùng CMake cho app (`client/macos/Deskhub.xcodeproj`).
Nguồn được nạp qua **file-system-synchronized group** — thêm file vào `app/` là Xcode
tự biên dịch, không phải sửa pbxproj. `libcore.a` do một **shell script phase** gọi
CMake dựng, nên `core/CMakeLists.txt` vẫn là nguồn sự thật duy nhất về danh sách source
của core.

```
make build-macos      # Debug  -> out/build/macos/Debug/app.app
make release-macos    # Release
make run-macos        # build rồi mở app
```

**Ràng buộc phiên bản** (đã dựng thật):

| Thứ | Bản |
|-----|-----|
| Xcode | 26.6 (17F113) |
| macOS Deployment Target | 14.0 |
| Swift | 6.0 |
| C++ | gnu++20 |
| Kiến trúc | universal (arm64 + x86_64) |

Ký **ad-hoc** (`CODE_SIGN_IDENTITY = "-"`) nên chạy được ngay trên máy dev; bản phát
hành cần Developer ID + notarize.

Frameworks link: VideoToolbox, CoreMedia, AVFoundation, CoreVideo, **ScreenCaptureKit**,
CoreGraphics, ApplicationServices, AppKit.

**Chạy thử hai máy:** máy A bấm **Share this Mac** → chọn cửa sổ/màn hình → **Start
sharing** → đọc địa chỉ trên `SessionView`. Máy B (macOS hoặc Windows) gõ địa chỉ đó
vào **Connect**. Qua Internet: bật Tailscale hai đầu, dùng IP `100.x.y.z`.

## 6b. Quy ước ngôn ngữ

Theo yêu cầu dự án (`08` §4b): **mọi chuỗi người dùng hoặc console thấy đều bằng tiếng
Anh**, **mọi comment trong code bằng tiếng Việt có dấu**.

## 7. Cái gì dùng lại được, cái gì phải viết

Con số để đo lời hứa "thêm một nền tảng = chỉ viết lớp backend mỏng":

- **Dùng lại nguyên**: toàn bộ `core/` (wire, packetizer, reassembler, FEC, session
  hai phía, input ordering, bitrate controller, retransmit cache) + `platform/Clock.h`.
- **Chép từ iOS gần như không sửa**: `UdpSocket`, `SourceQuery`, `VtDecoder`, `Log.h`.
- **Port có sửa**: `ClientLoop` (thêm kênh input desktop + clipboard), `AgentLoop`
  (đổi ba backend, đổi mô hình chặn → thread).
- **Viết mới hoàn toàn**: `ScreenCapture`, `VtEncoder`, `InputInjector`,
  `LocalInputMonitor`, `SourceEnum`, `Permissions`, `ClipboardSync`, `NetInfo`,
  `MacKeyMap`, và toàn bộ tầng SwiftUI.

## 8. Hạn chế hiện tại

- **Chưa chạy kiểm chứng hai máy thật.** Code biên dịch sạch (Debug + Release,
  universal) và bám sát bản tham chiếu Windows từng bước, nhưng các con số độ trễ /
  fps / e2e trong `docs/09-diagnostics.md` chưa được đo trên macOS. Đây là việc kế
  tiếp, và là thứ duy nhất còn ngăn cột macOS đổi từ 🔶 sang ✅.
- **e2e đo lúc enqueue**, chưa phải lúc frame lên màn hình — kế thừa nguyên caveat của
  nhánh `AVSampleBufferDisplayLayer` (`12-ios-client.md` §2). Muốn chính xác hơn thì
  chuyển sang `VTDecompressionSession`.
- **Chưa có mã hoá** (GĐ6 của `05-roadmap.md`) — như mọi nền tảng khác.
- **Chưa notarize**: người tải bản CI về phải tự bỏ quarantine.
- **Anti-cheat kernel** chặn được input tổng hợp trên macOS y như trên Windows. Đây là
  giới hạn chung, không riêng nền tảng nào (`01-architecture.md` §7).
