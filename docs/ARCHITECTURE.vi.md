[English](ARCHITECTURE.md) · **Tiếng Việt**

# Deskhub — Kiến trúc

Tài liệu này mô tả Deskhub **được xây như thế nào**: các tầng, tiến trình và luồng,
giao thức trên đường truyền, và các quyết định thiết kế đứng sau. Sản phẩm làm được gì
dưới góc nhìn người dùng nằm ở [`SPECIFICATION.vi.md`](SPECIFICATION.vi.md); mô hình
mối đe doạ nằm ở [`SECURITY.vi.md`](../SECURITY.vi.md).

Đây là bản dịch của [`ARCHITECTURE.md`](ARCHITECTURE.md); khi hai bản khác nhau, bản
tiếng Anh là bản chuẩn.

- **Trạng thái:** mô tả mã nguồn hiện tại.
- **Đối tượng đọc:** bất kỳ ai sửa mã.

---

## 1. Các tầng

Một quy tắc chi phối toàn bộ bố cục: logic viết một lần và dùng chung cho mọi client.

```
core/       C++20 thuần, không header OS, không mã bên thứ ba, test offline được
platform/   lớp trừu tượng OS mỏng, mỗi header một API giống hệt nhau mọi nơi (phụ thuộc core)
client/     app theo từng OS: windows, linux, macos, ios, android (phụ thuộc platform + core)
```

| Tầng | Nội dung |
| --- | --- |
| `core/protocol` | Định dạng gói (`Wire.h`), cắt khung record cho stream (`RecordStream.h`), bộ phân loại gói QUIC với gói beacon |
| `core/transport` | Packetizer/Reassembler cho video, FEC, cache gửi lại, bộ điều tốc gửi |
| `core/session` | Máy trạng thái phiên host/client, bảng viewer, đồng bộ clipboard, bảng phiên terminal, bộ khoá đếm lần đoán mã |
| `core/control` | Điều khiển bitrate, thang chất lượng, cỡ luồng, lệch đồng hồ |
| `core/terminal` | Bộ giả lập VT mọi client dùng chung: `VtParser`, `Screen`, `KeyEncoder`, `Palette` |
| `core/net` | Trust store (phía client), danh sách máy đã ghép (phía host), chọn địa chỉ bind, logic quét LAN |
| `core/ui` | Mọi chuỗi hiển thị, đọc/ghi cài đặt, dựng dòng bảng — để cả năm client nói giống hệt nhau |
| `platform/net` | `UdpSocket` (theo OS), `QuicEndpoint` (quiche sau pimpl), `SessionTransport`, truy vấn nguồn, dò host, quét LAN |
| `platform/session` | `HostEngine`, `HostNetLoop`, `ClientEngine`, `TerminalHost`, `TerminalViewer`, `AuthNegotiation` |
| `platform/system` | Đồng hồ, ngẫu nhiên, PTY (ConPTY / forkpty), danh tính máy (khoá), file trust/paired, autostart, giữ máy thức |
| `client/<os>` | Thu hình, mã hoá, giải mã, vẽ, cửa sổ, hộp thoại — không có gì mang hình dạng giao thức |

`core/` phải test offline được, không mạng không GPU. `platform/` được đụng OS nhưng
API công khai phải giống hệt nhau trên mọi hệ. Nếu cùng một đoạn mã xuất hiện ở hai
client, nó thuộc về tầng thấp hơn.

## 2. Một cổng, một transport

Mọi thứ host cung cấp đi trên **một cổng UDP** (mặc định 47777) qua một
`SessionTransport` duy nhất, bọc một `QuicEndpoint`:

```
                      Cổng UDP 47777
                            |
                 ClassifyPacket (byte đầu)
                   /                    \
            gói QUIC              gói Deskhub thuần
                 |                        |
   +-------------+------------+       chỉ beacon:
   |             |            |       LIST_SOURCES / PING được trả lời
 stream      datagram      (TLS)      ở bản rõ; mọi gói thô khác
   |             |                    đều bị bỏ
 control      video
 input        audio       Stream chở các record có tiền tố độ dài
 clipboard                (RecordStream), tối đa 16 KiB mỗi record.
 terminal                 Mỗi datagram chở đúng một gói video hoặc
                          một gói audio (≤ 1200 B).
```

- **Stream** (tin cậy, đúng thứ tự): control, input, clipboard, terminal — mỗi kết
  nối dùng một stream hai chiều do client mở. Stream nghẽn ở kết nối này không làm
  đứng kết nối khác.
- **Datagram** (không tin cậy, không thứ tự, vẫn mã hoá): gói video và gói audio.
  QUIC không bao giờ gửi lại datagram mất; với video thì FEC/NACK của app tự xử lý,
  còn với audio thì không gì xử lý cả — xem mục 9.
- **UDP thô** chỉ còn cho dò tìm: beacon trả lời máy quét không nói QUIC, và gói dò
  không mời nhận danh sách rỗng. Gói thô đến mà không phải loại dò tìm bị loại trước
  khi chạm tới bất kỳ mã phiên nào.

`QuicEndpoint` giấu kín quiche (pimpl; `QuicEndpointNone.cpp` thế chỗ, nhưng chỉ khi
build chủ động tắt bằng `-DDESKHUB_QUIC=OFF` — thiếu quiche thì configure lỗi ngay,
vì binary dùng stub không chia sẻ hay kết nối được). Kết nối được định danh bằng địa
chỉ peer; không có connection migration. Một
kết nối quiche chỉ dùng được từ một luồng, nên mọi lần chạm endpoint đều nằm dưới
mutex gửi của transport — và transport không bao giờ giữ mutex đó xuyên qua một lần
chờ socket (`WaitReadable` trước, không khoá; rồi `Poll` ngắn có khoá). Giữ khoá
xuyên qua lần chờ sẽ bỏ đói mọi bên gửi.

## 3. Quyền vào: ghép đôi

Mỗi máy sinh một khoá ECDSA P-256 ở lần chạy đầu (`HostIdentity`); băm SHA-256 của
SPKI là dấu vân tay người dùng nhìn thấy. TLS dùng chứng chỉ tự ký trên khoá đó. Trên
TLS, một cuộc bắt tay tầng ứng dụng (`AuthNegotiation`) quyết định quyền vào theo
từng kết nối. Transport chạy nó và bỏ mọi message từ kết nối chưa chốt xong auth:

| Client đưa ra | Host biết máy đó | Kết quả |
| --- | --- | --- |
| không gì cả | đã ghép đôi | **Signature**: client ký transcript nonce+dấu-vân-tay-host bằng khoá của nó. Vào êm. |
| không gì cả | máy lạ | **Approval**: người ngồi tại host được hỏi (*Let this machine in?*). |
| một passcode | host có mã | **Passcode**: SPAKE2 trên verifier có salt — mã không bao giờ đi qua mạng, mỗi kết nối một lần đoán, hai bên cùng chứng minh, MAC trói vào đúng khoá host mà client đang thấy (chặn chuyển tiếp). Mã đã gõ luôn bị kiểm, quen hay lạ. |
| một passcode | host không có mã | không có gì để đối chiếu → Signature nếu đã ghép, Approval nếu chưa. |
| bất kỳ | tắt ghép đôi mới | **Denied** (máy đã ghép vẫn đi đường Signature). |

Thành công thì client được ghi vào `paired_devices` của host; ghép đôi theo khoá,
không theo địa chỉ. Đoán sai passcode 3 lần khoá đường passcode 30 giây
(`AuthThrottle`, dùng chung hằng số với khoá phiên cũ); đường approval không cần
khoá — người thật là cái cổng.

Phía client, `known_hosts` (`TrustStore`) ghim khoá host. Khoá **đổi** thì chặn kết
nối sau một cảnh báo lớn; khoá chưa gặp được chính cuộc bắt tay phân xử (host chứng
minh được passcode thì được ghi nhớ mà không cần hỏi).

Trên wire là chính public key, không bao giờ là fingerprint trần — host tự hash thứ
nó nhận được, nên muốn khoác danh tính máy khác thì phải ký được bằng khoá mà kẻ mạo
danh không nắm. Và vì quyền vào chốt một lần cho mỗi kết nối, không tầng nào phía
trên transport hỏi lại: máy đã chứng minh mình không mang passcode trong bất kỳ
message nào về sau, và code phiên coi cả kết nối là đã xác thực.

## 4. Phía host

```
HostEngine (một cho cả app, sở hữu SessionTransport)
 ├─ luồng net-loop: RunHostNetLoop
 │    recv → trả lời beacon | nhận đường video | Chan::Terminal → TerminalHost
 │    Tick phiên theo từng nguồn, đẩy clipboard, reconfig, thống kê
 ├─ thu hình/mã hoá: theo từng nguồn, do callback thu hình của OS lái (tầng client)
 │    frame → encoder (mutex theo nguồn) → Packetizer → FEC → SendTo (datagram)
 └─ TerminalHost (khách thuê, khi terminal được chia sẻ)
      ├─ HandleMessage trên luồng net-loop: TERM_OPEN/DATA/RESIZE/CLOSE → PTY
      └─ luồng bơm: đầu ra PTY → Screen mirror phía host + record TERM_DATA,
           hết hạn, kick
```

- Engine chạy khi bất kỳ thứ gì được chia sẻ. Không có nguồn màn hình mà terminal
  được tick thì nó chạy không-nguồn; vòng lặp sống chừng nào terminal còn sống.
- Mỗi nguồn màn hình là một `SourcePipelineState`: `HostSession` riêng (bảng viewer,
  thương lượng, phân xử input), encoder, thang chất lượng và chẩn đoán riêng. Một
  lần mã hoá nuôi mọi viewer của nguồn đó.
- Vòng phản hồi: viewer gửi `Feedback` (loss/RTT) mỗi giây; `BitrateController`
  (AIMD) và `QualityLadder` của host chỉnh bitrate, độ phân giải, fps của encoder;
  FEC bật theo loss. CUBIC của quiche nằm dưới đường datagram; hai bộ hoạt động nối
  tiếp — quiche giới hạn thứ rời khỏi máy, app điều tốc encoder theo loss sinh ra.
- Input: "host thắng" — `LocalInputMonitor` tạm dừng input từ xa khi người ngồi tại
  máy động vào chuột thật; mỗi lúc một viewer điều khiển.
- Shell: mỗi shell một PTY (`ConPTY` trên Windows, `forkpty` nơi khác), tối đa 8;
  kết nối rớt thì shell được tách và PTY sống thêm 2 phút để đúng máy đó gắn lại.
  Mọi lần mở/đóng/tách/gắn lại đều ghi audit kèm địa chỉ, tên và khoá.
- Đầu ra của mỗi shell còn nuôi một Screen `core/terminal` phía host ngay từ lúc
  shell khởi động. *Stop & attach* ngắt client từ xa và mở mirror đó — scrollback
  còn nguyên — trong một cửa sổ terminal trên máy host; shell bị tiếp quản kiểu này
  thuộc về host, không bao giờ hết hạn, và kết thúc khi cửa sổ của host đóng.

## 5. Phía client

```
ClientEngine (một cho mỗi cửa sổ xem)         TerminalViewer (một cho mỗi cửa sổ shell)
 ├─ luồng net: kiểm tra tin cậy → auth →      ├─ luồng riêng: kết nối → kiểm tra tin cậy →
 │   HELLO/thương lượng → nhận video          │   auth → TERM_OPEN → bơm record
 │   (Reassembler+FEC), NACK, feedback        ├─ Screen của core/terminal giữ lưới ký tự
 └─ luồng giải mã: decoder + hàng đợi vẽ      └─ UI poll Snapshot(), post phím vào
```

Cả hai theo cùng quy tắc với host: kết nối QUIC sống trên một luồng; UI đăng ý định
(phím, đổi cỡ, chấp nhận dấu vân tay) vào hàng đợi lệnh. Cửa sổ terminal không bao
giờ tự phân tích escape sequence — `core/terminal` biến luồng byte thành lưới ô, cửa
sổ chỉ vẽ ô và chuyển tiếp sự kiện phím.

## 6. Dò tìm

Beacon trả lời `LIST_SOURCES` và `PING` bằng UDP thuần để máy quét quét được cả dải
mạng mà không tốn 254 lần bắt tay TLS. Máy lạ nhận danh sách rỗng; danh sách nguồn
thật chỉ lộ qua kết nối đã được cho vào. Câu trả lời đó còn mang theo những gì host
làm được — có nhận thao tác không, có chia sẻ terminal không — trong các cờ ở header
`SOURCE_LIST`, nên client biết trước khi mở bất kỳ cửa sổ nào rằng một chiếc điện
thoại chỉ có thể xem. Host bản cũ, có từ trước khi có các cờ này, không bật cờ nào. Thiết bị gần đây, trạng thái online
(ping/pong) và kết quả quét LAN đổ vào một danh sách thiết bị gộp trên Windows.

## 7. Dữ liệu trên đĩa

Tất cả nằm trong thư mục Deskhub của người dùng (`~/.deskhub`,
`%USERPROFILE%\.deskhub`): `host_key.pem` + `host_cert.pem` (danh tính),
`known_hosts` (host mà máy này tin), `paired_devices` (máy mà host này cho vào),
`auth_salt` (salt không bí mật), `ui-settings.txt`, `recent-devices.txt` (địa chỉ +
passcode che đi), và log theo từng lần chạy. I/O file nằm ở `platform/`; phần phân
tích và cấu trúc dữ liệu nằm ở `core/` và có unit test.

## 8. Kiểm thử

| Bộ | Chạy | Phủ |
| --- | --- | --- |
| `make test` | offline, không socket | toàn bộ `core/`: wire, framing, FEC, phiên, bộ giả lập VT, cài đặt, chuỗi, fuzz có cấu trúc |
| `make test-platform` | socket loopback | bắt tay QUIC thật, SPAKE2 đầu-cuối, terminal host + viewer qua mạng, PTY với shell thật, lockout, approval |
| `make test-integration` | loopback, thu/mã hoá giả | phiên host↔client đầy đủ: thương lượng, video qua mạng, input, cổng passcode/approval, chịu gói rác |
| các target fuzz | 30 giây mỗi target ở mọi PR, 15 phút mỗi target hằng đêm | parser cho wire, H.264, ráp gói, byte terminal và chuỗi UI, cộng máy trạng thái phiên phía host và viewer |

CI còn ép clang-format và clang-tidy (đều ghim phiên bản), SwiftLint `--strict`,
Android Lint, actionlint + shellcheck, chạy cả ba bộ dưới ASan/TSan, CodeQL cho
C++/Kotlin/Swift, quét gitleaks toàn bộ lịch sử, và coverage `core/` ≥ 90% dòng / 80%
nhánh. Ba bộ test còn được biên dịch chéo và chạy trên Linux arm64, emulator Android và
iOS Simulator.

## 9. Các quyết định đáng nhớ

- **Cổng khung hình đếm tới một mốc hạn, không đếm từ khung nó vừa giữ**: một compositor
  đưa sang 40 fps trong khi mục tiêu là 30 fps thì hầu hết các mốc 33 ms đều không có
  khung nào rơi đúng vào, nên một cổng chỉ hỏi "khung này có cách khung tôi giữ đủ xa
  không?" sẽ loại một khung xen kẽ và dừng ở 20 fps — vừa dưới mục tiêu, vừa lởm chởm,
  tức là giật hình chứ không phải luồng chậm hơn. `FrameGate` thay vào đó mang theo một
  mốc hạn chạy đều: mỗi lần nhận khung, mốc tiến đúng một chu kỳ, nên phần dư được giữ
  lại và 40 vào cho ra 30. Capture chậm hơn mục tiêu không bao giờ bị chặt bớt, và một
  mốc hạn đã tụt lại sau thời gian thực sẽ đồng bộ lại thay vì tích lũy, nên một quãng
  lặng không mua được một cú dồn khung về sau.

- **Host Linux encode trên thread riêng, và đưa cho thread đó khung hình nhỏ chứ không
  phải khung lớn**: encode ngay trong callback `process` của PipeWire từng ghìm capture
  xuống `1000 / enc_ms` fps và biến mọi dao động thời gian encode thành rung nhịp khung
  hình phía client. Giờ encode chạy trên thread riêng, được nạp qua `FrameMailbox`, một
  hàng đợi một-chỗ kiểu mới-nhất-thắng — khi encoder chậm chân, frame mới nhất thắng và
  frame cũ được đếm chứ không xếp hàng. Thứ đi qua hàng đợi là khung hình đã thu nhỏ về
  kích thước encode, còn khoảng một phần bảy số byte. Chuyển khung nguyên độ phân giải
  qua đó tốn hơn nhiều so với bản thân phép copy: 20 MB cache line bị bỏ lại ở trạng
  thái dirty trong core capture, và core encode phải kéo sang, đo được 16 ms so với
  3,4 ms cho cùng phép đọc trên vùng nhớ nó không sở hữu. Thread capture đằng nào cũng
  phải chạm mỗi pixel nguồn đúng một lần, nên đó là chỗ đúng để tiêu lượt duyệt duy
  nhất ấy. Frame dma-buf vẫn encode tại chỗ: compositor tái dùng bộ nhớ của chúng ngay
  khi callback trả về nên chúng không sống lâu hơn callback, và VA-API đằng nào cũng
  thu nhỏ chúng trên GPU.
- **Host Linux chọn encoder theo nơi frame nằm, không phải theo thứ được cài**: frame
  dma-buf đi vào VA-API, nơi import được zero-copy trên đúng GPU đã tạo ra nó; frame
  mapped (CPU) đi vào NVENC khi có driver NVIDIA, vì trên desktop do GPU NVIDIA render,
  compositor thương lượng lại screencast về shared memory, và việc encode khi đó thuộc
  về card lấy được pixel thẳng từ bộ nhớ hệ thống. `HwEncoder` đưa ra lựa chọn đó mỗi
  lần dựng lại encoder, và một frame khác loại đến sau sẽ trả về `false` — đó là tín
  hiệu để dựng lại.
- **Phần thu nhỏ ảnh trước NVENC là của chúng ta, không phải của swscale**: NVENC nhận
  pixel packed 32-bit nhưng không tự resize, còn ảnh capture là nguyên độ phân giải màn
  hình. `libswscale` đo được 9,2 ms cho 3440x1440 → 1280x534 — khoảng 2 GB/s, kém băng
  thông bộ nhớ của máy này cả một bậc, vì rescale packed-RGB rơi ra ngoài các đường đã
  tối ưu của nó. `RgbDownscale` trong `core/` là một bộ trung bình theo vùng viết đúng
  cho hình dạng này: mỗi pixel nguồn một lần nạp 32-bit, cộng dồn số nguyên, 4,0 ms cho
  cùng khung hình đó, và khử răng cưa đúng cách thay vì một mẫu bilinear như swscale.
  Tổng chi phí NVENC cho cả khung rơi vào ~5 ms, nên 60 fps còn dư chỗ.
- **Các con số hiệu năng chỉ có ý nghĩa khi đo trên bản release**: `make build-linux`
  và `make run-linux` cấu hình preset `x64-debug`, tức `-O0`, trong khi đường encode giờ
  là số học pixel nằm trong `core/`. Cùng một khung hình tốn ~19 ms ở đó so với ~5 ms từ
  `make release-linux`. Một báo cáo giật hình đo trên binary debug là đang đo kiểu build.

- **Viewer Apple hiển thị video theo PTS trên một control timebase, và pacer không bao
  giờ tin chính nó**: hiển thị mỗi frame ngay lúc nó đến khiến jitter Wi-Fi hiện ra
  thành giật hình trong khi mọi con số độ trễ vẫn đẹp — nhịp không phải là độ trễ.
  `VideoPacer` (core, test offline được) ánh xạ PTS của host sang giờ hiển thị local
  theo đúng cách metric e2e làm — minimum theo cửa sổ của `arrival − pts` — cộng một
  khoảng đệm ~33 ms để jitter được trả từ đó, và `VtDecoder` lái control timebase của
  `AVSampleBufferDisplayLayer` theo nó, chỉ resync khi lệch quá 250 ms. Một cú nhảy pts
  quá 2 s được đọc là stream mới chứ không phải jitter, nên ánh xạ được dựng lại thay
  vì đứng hình suốt một cửa sổ. Vì không thể chứng minh từ đây rằng renderer tôn trọng
  timebase ngoài trên mọi phiên bản OS, decoder tự canh lưng mình: một chuỗi frame bị
  hàng đợi renderer đầy nuốt mất sẽ lật về display-immediately và flush — thà mất phần
  mượt còn hơn mất hình.

- **Audio là một khung một datagram, và mất thì không đuổi theo**: một khung Opus 20 ms
  ở 64 kbps đo được khoảng 160 byte, rộng nhất 209 byte, so với 1180 byte một datagram
  chứa được — nên đường audio không có packetizer, không FEC, không reassembler, không
  NACK, tức là bỏ đi gần hết những gì đường video có. Mất gói được hấp thụ ở chỗ rẻ
  nhất: Opus mang sẵn FEC trong khung kế tiếp, và bên nhận bảo bộ giải mã che chỗ hổng
  mà jitter buffer báo. Gửi lại còn tệ hơn vô ích, vì một khung đến muộn 200 ms thì
  không phát được nữa nhưng vẫn kịp làm chậm mười khung sau nó. `make opus-smoke` đo
  đúng những con số đó trên bất kỳ máy nào dựng được thư viện.
- **Đồng hồ audio điều khiển jitter buffer, không phải đồng hồ tường**:
  `AudioJitterBuffer` không có timer nào bên trong. Bộ phát kéo một khung mỗi lần gọi,
  còn độ trễ mục tiêu chỉ là số khung nó nạp trước khi bắt đầu — 60 ms là ba khung. Nhờ
  vậy toàn bộ phần này test được offline mà không phải ngủ, và các kiểu hỏng đều hiện
  rõ: bùng gói thì bị chặn thay vì xếp hàng, hết gói thì nạp lại thay vì giật, số thứ
  tự nhảy xa thì coi là luồng mới thay vì hàng nghìn khung mất.
- **Tiếng cần cả hai đầu đồng ý, và client cũ không bao giờ nghe thấy**: viewer đặt bit
  0 của `Hello.features`, host quảng bá `kHostSharesAudio` trong phần năng lực của nó,
  và host chỉ gửi gói cho viewer nào có bit đó. Chính điều này giữ `kProtocolVersion` ở
  mức 2: viewer 5.0.x gửi `features = 0`, nên host 5.1 không bao giờ đặt lên dây một
  thông điệp mà nó không phân tích được.

- **Link terminal tự giữ sống và tự quay số lại**: viewer terminal có QUIC connection
  riêng, tách khỏi phiên video, nên không keepalive nào của đường video chạm tới nó.
  Để yên ở dấu nhắc thì nó không có lưu lượng gì cả và chết vì idle timeout 30 giây
  của QUIC; sau đó viewer dừng luôn thread ở trạng thái `Reattaching` mà không hề
  quay số lại — trong khi shell vẫn đang chờ trên host suốt 2 phút và không ai quay
  lại lấy. Giờ `TerminalViewer` gửi gói ack-eliciting theo chu kỳ và quay số lại có
  backoff, dùng `TerminalClient::Reattach()` (vốn đã viết và có test trong core,
  chỉ là chưa ai gọi) để lấy lại đúng shell cũ kèm scrollback.
  `deskhub::KeepaliveIntervalUs` / `ReconnectDelayUs` giữ các mốc thời gian trong
  core: keepalive tối đa bằng nửa idle timeout để mất một gói vẫn sống, và việc thử
  lại dừng đúng ở `kTerminalReattachGraceUs`, vì quá mốc đó host đã bỏ shell rồi,
  kết nối lại chỉ âm thầm mở một shell mới.
- **Một record lên stream thì lên trọn vẹn, còn client tụt lại thì được vẽ lại chứ
  không nhận từng byte**: mọi thứ tin cậy — control, auth, output terminal — đều là
  record có tiền tố độ dài dùng chung một QUIC stream, nên nửa record trên đường
  truyền làm lệch khung vĩnh viễn ở đầu bên kia; `RecordStream` không có cách nào
  đồng bộ lại và peer đóng luôn kết nối. `QuicEndpoint::SendStream` trước đây ghi
  được bao nhiêu hay bấy nhiêu rồi bỏ phần dư, chịu được cho tới khi một lệnh như
  `make test` chạy nhanh hơn đường truyền: cửa sổ stream 1 MiB đầy, phần đuôi của một
  record `TermData` bị bỏ, framer của viewer hỏng và shell "mất kết nối" một phút sau
  khi mở. Giờ nó từ chối cả record khi stream không còn chỗ, và đóng kết nối nếu vẫn
  lỡ ghi được một phần, vì stream đã rách thì không vá tại chỗ được. Ở tầng trên,
  `TerminalHost` giữ output chưa gửi trong hàng đợi riêng của từng shell và thử gửi
  lại ở mỗi nhịp, nên một đợt xả chỉ nhanh hơn đường truyền trong chốc lát — output
  của một lần build chẳng hạn — vẫn tới client đủ từng byte. Vượt `kMaxPendingBytes`
  thì hàng đợi bị bỏ chứ không
  phình thêm: mọi byte đều đã vào `Screen` gương phía host rồi, nên client được kéo
  cho kịp bằng `deskhub::term::RenderScreen` — vẽ lại lưới hiện tại, nhiều nhất một
  lần mỗi `kRepaintIntervalUs`. Phần output không ai kịp đọc thì bỏ chứ không đệm, nhờ
  vậy lệnh xả dữ liệu vẫn chạy đúng tốc độ của nó mà màn hình cuối cùng vẫn đúng.
  Client gắn lại phiên cũng nhận đúng bản vẽ lại đó, vì sau một quãng đứt thì vị trí
  của nó trong dòng byte không còn nghĩa gì.
- **Chia sẻ tự động phải chờ desktop, không phải liệt kê một lần rồi thôi**: Windows
  đăng ký autostart bằng scheduled task `ONLOGON`, chạy trước khi phiên có màn hình
  nào để liệt kê, nên một lần gọi `ListDisplays()` lúc dựng cửa sổ trả về rỗng và app
  báo là không có gì để chia sẻ. `deskhub::ui::AutoShareGate` (trong core, có unit
  test) giữ quy tắc thử lại — dò mỗi `kAutoShareProbeMs`, bỏ cuộc sau
  `kAutoShareGiveUpMs` — và mỗi client tự chạy nó bằng timer của mình, nên chính sách
  chỉ tồn tại một chỗ. `NextAutoShareStep` là đúng quy tắc đó nhưng không giữ trạng
  thái, và đó là thứ client Swift gọi qua `dh_auto_share_step`. Một lần chia sẻ tự
  động không bao giờ mở hộp thoại modal: lúc đăng nhập cửa sổ có thể đang ẩn trong
  khay hệ thống, nơi hộp thoại vừa không nhìn thấy vừa chặn việc chia sẻ vĩnh viễn,
  nên các lý do từ chối đi vào banner trang Host và log. Các client desktop cũng làm
  mới danh sách nguồn theo tín hiệu đổi màn hình của OS, nhờ vậy danh sách vẫn đúng
  khi cắm thêm màn hình về sau.
- **Chọn quiche thay vì msquic/ngtcp2**: thư viện QUIC duy nhất có bằng chứng chạy
  thật trên cả Android lẫn iOS. Nó mang theo BoringSSL, thứ phục vụ luôn SPAKE2 và
  danh tính máy — không cần thư viện mật mã thứ hai.
- **Không có connection migration**: không thư viện ứng viên nào hỗ trợ dùng được
  phía client. Kết-nối-lại-và-gắn-lại (kiểu tmux, vốn đã bắt buộc cho mobile chạy
  nền) là đủ.
- **ECDSA P-256, không phải Ed25519**: phía server của BoringSSL không ký bắt tay
  TLS bằng Ed25519 qua quiche. Đừng đổi lại. Khoá Ed25519 còn lưu trên đĩa được
  thay ngay khi nạp — để nguyên thì mọi bắt tay chết với `QUICHE_ERR_TLS_FAIL` mà
  màn hình không hiện gì.
- **Verifier của passcode là một lần SHA-256, không phải KDF đắt tiền**: SPAKE2 đã
  giới hạn kẻ tấn công còn đúng một lần đoán online mỗi kết nối và không để lại
  transcript nào đáng mang về crack offline — đó chính là việc mà độ nặng của KDF
  sinh ra để làm.
- **quiche là thư viện build sẵn, không phải FetchContent**: `scripts/build-quiche.sh`
  ghi mỗi rust target một thư mục dưới `third_party/quiche/` cộng một `include/`
  dùng chung — quiche.h và bộ header BoringSSL mà boring-sys vendor, được chép ra vì
  Deskhub gọi thẳng BoringSSL cho danh tính host và muốn một đường include duy nhất,
  không thư viện TLS thứ hai. `DeskhubQuiche.cmake` biến chỗ đó thành
  `deskhub::quiche`; thiếu thư viện là configure lỗi.
- **Apple link `libplatform_bundled.a`**: app Xcode tiêu thụ archive platform từ
  ngoài CMake, nơi link PRIVATE tới quiche không bao giờ tới được dòng link của
  chúng — nên một bước `libtool` gộp platform + quiche thành đúng một archive mà
  `.pbxproj` link.
- **Bãi mìn toolchain Windows đã được dọn — giữ nguyên như vậy**: quiche build với
  CRT tĩnh qua `CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_RUSTFLAGS` cho phần Rust cộng
  `/MT` trong `CFLAGS_x86_64_pc_windows_msvc` cho phần BoringSSL (mặc định của msvc
  là runtime DLL, còn ép flag qua `RUSTFLAGS` chung thì cargo build hỏng thẳng), cả
  cây pin `MultiThreaded` cho khớp để exe không cần VC++ Redistributable; wxWidgets
  re-pin `wxBUILD_USE_STATIC_RUNTIME` mỗi lần configure vì `wx_option()` cache vĩnh
  viễn. BoringSSL phải build dưới generator Visual Studio mặc định — crate cmake chỉ
  truyền được /MT qua flag per-config ở đó, nên ép `CMAKE_GENERATOR=Ninja` là
  BoringSSL âm thầm quay về /MD và bước link cuối chết với LNK2038; nếu MSBuild dính
  MSB6003 vì path dài thì bật Windows long paths thay vì đổi generator. `link.exe`
  của Git Bash trong `/usr/bin` che mất linker MSVC (đặt thư mục của `cl.exe` lên
  trước), cơ chế rewrite path của nó bóp méo tham số kiểu `/...`
  (`MSYS2_ARG_CONV_EXCL`), và installer của NASM không đụng vào PATH.
- **quiche cho Android bỏ qua cargo-ndk trên máy Windows**: cargo-ndk đưa cho
  boring-sys đường dẫn `clang` không có phần mở rộng, CMake trên Windows từ chối nó,
  nên `build-quiche.sh` tự đặt `CC_*`/`CXX_*`/`AR_*`, linker của cargo và `--target=`
  cho từng ABI rồi gọi cargo thuần. BoringSSL ở đó vẫn cần Ninja (generator Visual
  Studio không nhắm được NDK), còn bindgen lấy libclang của Visual Studio — nó tìm
  `stddef.h` cạnh binary của chính nó — nên `BINDGEN_EXTRA_CLANG_ARGS` trỏ sang
  resource header của NDK bằng dấu gạch chéo xuôi, vì bindgen tách biến đó theo luật
  shell và nuốt mất dấu gạch chéo ngược.
- **Mỗi app cross-compile tự build quiche của mình trước**: `build-android`,
  `build-ios`, `build-macos` và `build-linux` phụ thuộc vào một target quiche cho ABI
  của chúng, giống như `debug`/`release` làm cho host. quiche là per-ABI và bước
  configure của CMake thất bại nếu thiếu, nên một bản build bỏ qua bước này trông như
  hỏng toolchain chứ không như thiếu thư viện — và một app kẹt lại ở lần build thành
  công cuối cùng sẽ nói thứ giao thức mà các máy khác không còn trả lời.
- **quiche cho iOS pin `IPHONEOS_DEPLOYMENT_TARGET=17.0`**: clang của boring-sys
  trôi theo mặc định SDK trong khi rustc link theo minimum của riêng nó, và độ lệch
  hiện ra thành `___chkstk_darwin` undefined lúc link.
- **Hai đồng hồ, có chủ đích**: `NowUs()` là monotonic (giây uptime) cho khoảng
  thời gian; `NowUnixSeconds()` là cái duy nhất hiện ra thành ngày tháng. Trộn lẫn
  không kêu — một mốc monotonic đem lưu sẽ hiện thành một thời điểm nào đó trong
  ngày 1 tháng 1 năm 1970.
- **Tiến trình con của PTY Windows không nhận handle chuẩn nào**: khi stdout của
  chính host bị redirect, Windows truyền redirect đó xuống con bất chấp thuộc tính
  pseudo-console và shell nói chuyện với pipe; không đưa handle nào thì nó quay về
  console — chính là ConPTY vừa gắn.
- **`wxWANTS_CHARS` trên lưới terminal Windows**: thiếu nó thì điều hướng dialog
  của frame nuốt Enter, Tab và các phím mũi tên trước khi terminal kịp thấy.
- **TCC của macOS gắn quyền với chữ ký code**: app.app build tay (ký ad-hoc, đổi
  chữ ký mỗi lần build) và bản dmg ký Developer ID giành nhau đúng một dòng
  `com.deskhub.macos` — System Settings hiện đã cấp quyền trong khi bản vừa chạy bị
  từ chối, âm thầm với Accessibility. `make reset-macos-permissions` xoá mọi quyền
  để lần chạy sau hỏi lại.
- **Một CRT release tĩnh trên Windows, cho mọi cấu hình**: cargo build quiche với
  CRT release tĩnh (mặc định của msvc — đừng bao giờ ép qua `RUSTFLAGS`, flag đó
  ngấm vào proc-macro và giết cargo), và cả cây CMake pin `MultiThreaded` cho khớp
  — cũng chính là thứ giữ app là một exe duy nhất không cần VC++ Redistributable.
  Rust không có bản debug-CRT nên Debug cũng phải khớp: `_ITERATOR_DEBUG_LEVEL=0`,
  `/U_DEBUG`, bỏ `/RTC1` — CRT release không có `_CrtDbgReport` lẫn hỗ trợ
  run-time check. Lệch bất kỳ chỗ nào là dính một tràng LNK2038.
- **Passcode = cửa tự phục vụ, approval = đường lui**: mã đã gõ luôn được kiểm;
  không mã thì người quyết. Passcode không bao giờ qua mạng dưới bất kỳ dạng nào kẻ
  tấn công mang về được.
- **Bộ giả lập VT là của dự án**: không widget terminal nào có mặt trên cả năm
  client với giấy phép dùng được, và tự sở hữu nó làm hành vi terminal test được
  offline và giống hệt nhau mọi nơi.
- **Mirror shell phía host được nuôi từ byte đầu tiên**: đầu ra PTY là luồng
  một-người-đọc và huỷ khi đọc — byte đã đọc và gửi cho viewer không phát lại được —
  nên lưới mà *Stop & attach* mở ra phải được dựng ngay khi byte đi qua, không phải
  lúc bấm nút. Khi viewer từ xa còn gắn, phản hồi truy vấn terminal của chính mirror
  bị vứt bỏ: màn hình của viewer đã trả lời rồi, và shell không được nghe hai câu
  trả lời.
- **Một cổng**: beacon, màn hình và terminal dùng chung một listener; QUIC ghép kênh
  kết nối và stream. Cổng thứ hai ngày trước tồn tại chỉ vì đường màn hình tiền-QUIC
  chiếm trọn socket.
- **Mọi icon đều được dẫn xuất, và chỉ một phần được bo góc**: `make icons` dựng lại
  toàn bộ bộ icon từ một file gốc duy nhất `assets/icon_1024.png`. macOS, iOS, trang
  Play Store và đường adaptive-icon của Android tự cắt artwork theo hình dạng riêng
  của chúng, nên các asset đó giữ nguyên hình vuông tràn viền; Windows, Linux và
  launcher Android trước API 26 vẽ đúng những gì được đưa, nên icon của chúng phải có
  sẵn góc bo và phần trong suốt nướng vào ảnh — nếu không, app hiện ra như một ô vuông
  xanh cứng cạnh mọi icon bo góc khác. `scripts/make-icons.py` cố ý chỉ dùng thư viện
  chuẩn: bootstrap không cài công cụ xử lý ảnh nào.
