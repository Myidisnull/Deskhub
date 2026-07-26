# 15 — TODO sau review `core/` + `platform/`

Kết quả rà soát toàn bộ `core/` (7.7k dòng) và `platform/` theo tiêu chuẩn ngành, ngày
**2026-07-26**. Mỗi mục ghi đủ: **hiện trạng** (file:dòng), **vì sao là vấn đề**, **cách sửa
đề xuất**, **cách kiểm chứng đã xong**.

Trạng thái lúc rà soát: `make test` **pass**, `core` không include header OS nào (bất biến
kiến trúc còn nguyên), style CI xanh.

> **Đọc thứ tự nào trước:** làm §3 (build hygiene) TRƯỚC. Nó rẻ nhất và nó là thứ sẽ tự bắt
> giúp phần lớn lỗi còn lại — đặc biệt là ASan, thứ đang làm cho test fuzz sẵn có trở nên
> vô dụng.

---

## Bảng tổng

> **Cập nhật 2026-07-26 (lần 2).** A1, A3 và D3 đã triển khai xong ở tầng `core/` +
> `platform/` theo thiết kế trong Claude Design ("Deskhub App" — màn *Settings / password +
> trusted devices*). Chi tiết ở cuối file, mục **Đã làm**. Phần còn lại của A1 là giao diện
> + keychain trên 4 nền tảng.

| # | Mục | Nhóm | Công | Rủi ro nếu để nguyên |
|---|-----|------|------|----------------------|
| A1 | ~~Không có xác thực phiên~~ → **core xong**, còn UI/keychain | Bảo mật | Vừa | Bất kỳ ai trong LAN điều khiển được máy |
| A2 | Beacon khuếch đại phản xạ UDP | Bảo mật | Nhỏ | Host thành nguồn DDoS cho bên thứ ba |
| A3 | ~~`sessionId` entropy thấp~~ → **xong** | Bảo mật | Nhỏ | Đoán được → inject input/BYE |
| B1 | `kMaxNackIndices` không đạt được | Đúng đắn | Rất nhỏ | Cắt count âm thầm (latent) |
| B2 | Trừ thời gian không dấu không guard | Đúng đắn | Nhỏ | Ngắt kết nối oan; **treo** ở LatencyTrace |
| B3 | Overhead FEC 100% với frame nhỏ | Hiệu năng | Nhỏ | Nhân đôi gói đúng lúc mạng đang mất gói |
| C1 | Không cờ cảnh báo ngoài MSVC, không sanitizer | Build | Nhỏ | Test fuzz sẵn có không bắt được gì |
| C2 | Không fuzz tầng parse | Build | Vừa | Ranh giới tin cậy chưa được ép thật sự |
| C3 | Coverage không có ngưỡng | Build | Rất nhỏ | Phủ tụt dần không ai biết |
| C4 | Không có `.clang-tidy` | Build | Nhỏ | Bỏ lọt narrowing / bugprone |
| D1 | ClockSync + LatencyTrace là code chết | Kiến trúc | Vừa | Client tự viết lại bản không test |
| D2 | Discovery chỉ có trên Windows | Kiến trúc | Lớn | — (ghi nhận, không phải lỗi) |
| D3 | `platform/Clock.h` rò rỉ | Kiến trúc | Nhỏ | **(1)(4) xong**; (2)(3) còn lại |

---

## 1. Bảo mật

### ⬜ A1 — Không có xác thực: bất kỳ máy nào trong mạng đều điều khiển được host

**Hiện trạng.** `core/src/session/HostSession.cpp:37-62` — HELLO đầu tiên từ **bất kỳ ai**
đi thẳng: Idle → cấp `sessionId` → Ready → (START) → Streaming → `input_.HandlePacket` →
`InputInjector`. `inputAllowed_` mặc định `true` (`HostSession.h:166`). Đã grep toàn bộ
`client/windows`, `client/macos`: **không có** hộp thoại duyệt phía host, **không có** mã ghép
đôi, **không có** mật khẩu ở bất cứ đâu.

**Vì sao là vấn đề.** Mã hoá là khoản hoãn **có chủ ý và đã công bố** (`05-roadmap.md` GĐ6,
`PRIVACY.md:96`) — chuyện đó ổn và không nằm trong TODO này. Nhưng **uỷ quyền là một biện
pháp khác với mã hoá**, và nó hiện không nằm trên roadmap ở bất kỳ đâu. AnyDesk/RustDesk/VNC
đều bắt buộc mật khẩu hoặc duyệt từng phiên **kể cả trong LAN tin cậy**.

Kịch bản cụ thể: một laptop cùng Wi-Fi quán cà phê chạy `DISCOVER` → `LIST_SOURCES` →
`HELLO` → gõ được vào máy bạn. Không cần sniff, không cần giả mạo gì.

**Cần chốt hướng trước khi code** — hai lựa chọn, không loại trừ nhau:

- **(a) Mã phiên 6 chữ số.** Host hiện mã trên màn hình Share; client nhập; mã đi trong
  HELLO (dùng `Hello::features` hay thêm trường vào đuôi payload theo đúng mẫu tương thích
  ngược đã có ở `HelloAck::flags`). Rẻ, không cần crypto, chặn được truy cập tuỳ tiện.
- **(b) Hộp thoại duyệt phía host.** HELLO → host giữ ở trạng thái chờ, hiện "X muốn kết
  nối — Accept / Deny". Đúng mô hình AnyDesk nhất, nhưng cần thêm một trạng thái vào máy
  trạng thái và một đường callback lên UI của cả 4 nền tảng.

**Chỗ sửa (dù chọn hướng nào).** Cổng kiểm tra phải nằm **trước** `state_.store(State::Ready)`
ở `HostSession.cpp:57`, tức trong core — không phải ở từng client. Lý do y hệt lý do
`SetInputAllowed` được đặt trong core (xem comment `HostSession.h:106-115`): đây là **luật
giao thức**, mỗi nền tảng tự cài lại một luật giao thức là cách chắc chắn nhất để chúng lệch
nhau.

Từ chối dùng lại đường `SendReject()` sẵn có (`HostSession.cpp:198`) — client đã biết xử lý
`Codec::Rejected`. Cân nhắc thêm một mã lý do để client phân biệt "sai mã" với "đang bận",
nếu không người dùng nhập sai mã sẽ thấy thông báo "host rejected (busy or codec mismatch)".

**Kiểm chứng.** Ca test trong `core/tests/session/SessionTests.cpp`: HELLO sai mã → không
chuyển sang Ready, `sessionId()` vẫn 0, và một `INPUT_EVENT` gửi ngay sau đó không gọi
`onInput` lần nào.

---

### ⬜ A2 — `Beacon` là bộ khuếch đại phản xạ UDP

**Hiện trạng.** `core/src/discovery/Beacon.cpp:36-40` trả lời `LIST_SOURCES` — yêu cầu **12
byte**, `sessionId = 0`, không trạng thái, không xác thực — bằng `SOURCE_LIST` tới **~577
byte** (8 nguồn × (7 + 64 byte tên) + 1 + header). Hệ số khuếch đại **~48×**.
`DISCOVER` (12 B) → `ANNOUNCE` (tới 69 B) → ~5.7×.

Không có rate limit trong core, **và cũng không có ở call site**: `client/windows/cpp/
AgentLoop.cpp:924` gọi `beacon.Reply` rồi `sendto` ngay, vô điều kiện.

**Vì sao là vấn đề.** Địa chỉ nguồn UDP giả được dễ dàng. Kẻ tấn công broadcast `LIST_SOURCES`
với source IP giả là nạn nhân → mỗi host trong dải trả 577 byte vào mặt nạn nhân. Trên mạng
không lọc egress (BCP 38), host của người dùng trở thành nguồn DDoS cho bên thứ ba.

Comment đầu `Beacon.cpp:5-7` có lý luận về chống lụt, và lý luận đó **đúng nhưng chưa đủ**:
nó chỉ xét thiệt hại cho *chính host*, không xét nạn nhân bị giả mạo. Nên cập nhật comment
đó luôn khi sửa.

**Cách sửa.** Token bucket theo IP nguồn, đặt **trong `Beacon`** (để cả 4 nền tảng dùng
chung, đúng lý do core tồn tại) chứ không ở AgentLoop:

- `Beacon::Reply` nhận thêm tham số địa chỉ nguồn (dạng khoá mờ — `uint64_t` hash do caller
  tính, để core không phải biết `sockaddr`) và `nowUs`.
- Bảng nhỏ cố định (vd. 16 entry, ghi đè cũ nhất) đếm gói/giây theo khoá. Vượt ngưỡng
  (đề xuất 5 gói/s/nguồn, 50 gói/s tổng) → trả 0.
- Kẹp kích thước `SOURCE_LIST`: cân nhắc giảm `kMaxSourceNameBytes` trong câu trả lời
  broadcast, hoặc chỉ trả `sourceCount` và bắt client hỏi chi tiết sau khi đã có phiên.

**Kiểm chứng.** Ca test trong `core/tests/discovery/DiscoveryTests.cpp`: 100 `LIST_SOURCES`
liên tiếp từ cùng một khoá trong 1 giây → số lần `Reply` trả khác 0 ≤ ngưỡng; khoá khác
vẫn được trả lời bình thường trong cùng khoảng thời gian đó.

---

### ⬜ A3 — `sessionId` entropy thấp và một phần do kẻ tấn công chọn

**Hiện trạng.** `core/src/session/HostSession.cpp:53-55`:

```cpp
uint32_t sid = uint32_t(nowUs ^ (nowUs >> 32)) ^ m->clientId;
```

`m->clientId` đến từ **chính HELLO của kẻ tấn công**; `nowUs` là bộ đếm đơn điệu với bit
thấp đoán được. Cùng mẫu ở `core/src/discovery/HostRegistry.cpp:40` (`probeId`, tác động
nhẹ hơn nhiều).

**Vì sao là vấn đề.** Chính comment ở `HostSession.cpp:12-14` nói `sessionId` là *hàng rào
duy nhất* phân biệt "client của tôi" với phần còn lại của Internet. Đoán trúng là inject
được `INPUT_EVENT`, `BYE`, `NACK` mà không cần đọc được luồng.

**Cách sửa.** Nguồn ngẫu nhiên mã hoá phải nằm ở `platform/` (core cấm đụng OS):

- Thêm `platform/include/deskhubp/Random.h` — `deskhubp::RandomU32()`: `BCryptGenRandom`
  trên Windows, `getentropy`/`/dev/urandom` trên POSIX.
- `HostSession` nhận sessionId qua callback hoặc constructor thay vì tự sinh, để core vẫn
  không biết gì về OS và test vẫn bơm được giá trị xác định.

Sửa cùng lúc thì gộp chung một commit với A1 (cùng vùng mã, cùng chủ đề).

---

## 2. Đúng đắn & hiệu năng

### ⬜ B1 — `kMaxNackIndices = 593` không bao giờ đạt được: count là u8

**Hiện trạng.** `core/include/deskhub/wire/Wire.h:86` tính 593 từ MTU. `core/src/wire/
Wire.cpp:248` chấp nhận `indices.size() <= 593`, rồi `Wire.cpp:254` ghi:

```cpp
p[4] = uint8_t(indices.size());   // 300 → 44
```

Với 256–593 chỉ số, byte count bị cắt âm thầm và bên nhận đọc sai số lượng.

**Mức độ.** **Chưa live** — cả hai call site dùng `uint16_t nackIdx[64]`
(`client/windows/cpp/ClientApi.cpp:283`, `client/android/.../ClientLoop.cpp:563`). Nhưng
`HostSession.cpp:116` đang cấp `uint16_t idx[kMaxNackIndices]` = **1186 byte stack** cho một
trần vĩnh viễn không thể quá 255.

**Cách sửa (chọn 1).**

- Đơn giản: `kMaxNackIndices = 255`, sửa comment giải thích rằng trần thật là độ rộng của
  trường count chứ không phải MTU. Thu hồi luôn 1 KB stack ở HostSession.
- Hoặc: đổi count sang u16 và cập nhật `04-protocol.md`. Không đáng — client chỉ xin vài
  mảnh thiếu của frame đầu hàng, 255 là quá đủ.

**Kiểm chứng.** `core/tests/wire/WireTests.cpp` — `BuildNack` với 256 chỉ số phải trả 0.

---

### ⬜ B2 — Trừ thời gian không dấu: một module phòng thủ, các module khác thì không

**Hiện trạng.** `core/src/discovery/HostRegistry.cpp:109-113` guard rõ ràng, kèm comment nói
thẳng rằng `nowUs` **có thể lùi** giữa hai vòng lặp (đồng hồ đơn điệu vẫn đọc lệch giữa các
lõi):

```cpp
if (nowUs > hosts_[i].lastSeenUs && nowUs - hosts_[i].lastSeenUs > staleUs_)
```

Cùng biểu thức đó **không guard** ở:

| Chỗ | Hậu quả khi nowUs lùi |
|-----|------------------------|
| `core/src/control/LatencyTrace.cpp:28` | **Treo.** `while (nowUs - markUs_ >= sampleUs_)` — lùi 1 µs → vòng lặp chạy ~5.7×10¹³ lần |
| `core/src/session/ClientSession.cpp:181` | Ngắt kết nối oan: `Die("lost contact with host (timeout)")` |
| `core/src/session/HostSession.cpp:156` | Ngắt kết nối oan: `Disconnect()` |
| `core/src/transport/Reassembler.cpp:207` | Drop frame oan + xin IDR (IDR nặng — đúng lúc không cần) |
| `core/src/transport/Reassembler.cpp:66` | Thống kê `maxGapMs_` thành số rác |

**Vì sao là vấn đề.** Đây là **mâu thuẫn nội bộ**, không phải thiếu sót ngẫu nhiên: hoặc
comment ở HostRegistry sai (thì bỏ guard đó đi), hoặc nó đúng (thì áp dụng khắp nơi). Đang ở
giữa là trạng thái tệ nhất — người đọc sau không biết tin bên nào.

**Cách sửa.** Chốt là "nowUs CÓ THỂ lùi" (an toàn hơn, và Clock.h không hứa ngược lại), rồi
thêm một helper dùng chung trong `core/include/deskhub/wire/` hoặc một header tiện ích mới:

```cpp
// Hiệu thời gian an toàn: nowUs lùi so với mốc → 0 thay vì tràn thành số khổng lồ.
inline constexpr uint64_t ElapsedUs(uint64_t nowUs, uint64_t sinceUs) {
    return nowUs > sinceUs ? nowUs - sinceUs : 0;
}
```

Thay ở cả 5 chỗ trên **và** ở HostRegistry (để chỉ còn một cách viết). Ưu tiên
`LatencyTrace.cpp:28` — chỗ đó là treo, không phải sai số.

**Kiểm chứng.** Mỗi module một ca: gọi `Tick`/`Add`/`PopReady` với `nowUs` nhỏ hơn lần gọi
trước → không đổi trạng thái, không treo. Ca LatencyTrace phải có timeout để nếu hồi quy thì
CI đỏ chứ không treo runner.

---

### ⬜ B3 — Overhead FEC là 100% với frame nhỏ, không phải 1/8 như tài liệu ghi

**Hiện trạng.** `core/src/transport/Packetizer.cpp:38`: `numGroups = ceil(count / 8)`.
Overhead thật = `numGroups / count`:

| count (số gói của frame) | numGroups | Overhead |
|---|---|---|
| 1 | 1 | **100%** |
| 2 | 1 | 50% |
| 4 | 1 | 25% |
| 8 | 1 | 12.5% |
| 14 (P-frame ~16 KB) | 2 | 14% |

`core/include/deskhub/wire/Wire.h:62` khẳng định "chi phí băng thông vẫn y hệt =
1/kFecGroupSize" — chỉ đúng tiệm cận.

**Vì sao là vấn đề.** P-frame trên màn hình tĩnh thường xuyên là 1–2 gói. FEC được
`BitrateController` bật **đúng lúc đường truyền đã đang mất gói** (`BitrateController.cpp:32`,
ngưỡng ≥1%), tức là ta nhân đôi số gói của các frame nhỏ đúng vào lúc mạng đang chật.

**Cách sửa.** Bỏ parity khi `count < kFecGroupSize` — parity của một nhóm 1 phần tử **chính
là bản sao của gói đó**, điều mà `Wire.h:311` đã tự thừa nhận. Ngưỡng đề xuất: chỉ phát FEC
khi `count >= kFecGroupSize` (tức overhead ≤ 12.5% theo đúng thiết kế). Frame nhỏ mất gói thì
đã có NACK gánh (`RetransmitCache`) — rẻ hơn nhiều vì chỉ tốn khi thật sự mất.

Sửa xong nhớ cập nhật đoạn "chi phí băng thông" ở `Wire.h:53-63` và `06-phase3-transport.md`.

**Kiểm chứng.** `core/tests/transport/FecTests.cpp`: frame 1 gói với `SetFecEnabled(true)` →
`SendFrame` phát đúng 1 datagram, không có gói parity nào.

---

## 3. Vệ sinh build & kiểm chứng — **LÀM NHÓM NÀY TRƯỚC**

### ⬜ C1 — Không có cờ cảnh báo ngoài MSVC, không `-Werror`, không sanitizer ở đâu cả

**Hiện trạng.** `core/CMakeLists.txt:38`:

```cmake
if(MSVC)
    target_compile_options(core PRIVATE /W4 /permissive- /sdl)
endif()
# ← không có else()
```

Core được dịch bởi **GCC** (CI Ubuntu), **AppleClang** (CI macOS), **NDK clang** (Android),
**Xcode** (iOS) — tất cả ở **mức cảnh báo mặc định**, tức gần như câm.

Đã grep `sanitiz|asan|ubsan|fsanitize|Werror|Wall|Wextra` qua `Makefile`, `make/`, mọi
`CMakeLists.txt`, mọi workflow trong `.github/`: **không một kết quả nào**.

**Vì sao đây là mục quan trọng nhất trong file.** Nó đang vô hiệu hoá đúng bài test tốt nhất
mà dự án đã tự viết: `core/tests/wire/WireTests.cpp:516` — *"300 garbage buffers through every
Parse*"*. Không có ASan, test đó chỉ bắt được crash cứng; **những cú đọc tràn heap mà nó sinh
ra để tìm sẽ đi qua im lặng**. Bỏ công viết fuzz rồi chạy không sanitizer là trả tiền mà
không lấy hàng.

**Cách sửa.**

1. `core/CMakeLists.txt` — thêm nhánh non-MSVC:
   ```cmake
   else()
       target_compile_options(core PRIVATE -Wall -Wextra -Wconversion -Wshadow)
   endif()
   ```
   Làm tương tự cho target `core_tests`. Dự liệu: `-Wconversion` sẽ ra một loạt cảnh báo ở
   các chỗ thu hẹp kiểu có chủ ý (`LinkStats.cpp:88-91`, `Reassembler.cpp:270`) — thêm
   `static_cast` tường minh, đó chính là điểm của cờ này.

2. `CMakePresets.json` — thêm preset `asan`:
   ```json
   {
     "name": "asan",
     "inherits": "x64-debug",
     "cacheVariables": {
       "CMAKE_CXX_COMPILER": "clang++",
       "CMAKE_CXX_FLAGS": "-fsanitize=address,undefined -fno-omit-frame-pointer",
       "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=address,undefined"
     }
   }
   ```

3. `make/core.mk` — target `asan` chạy `core_tests` dưới sanitizer, cùng khuôn với target
   `coverage` sẵn có.

4. `.github/workflows/build.yml` — thêm một job (Ubuntu, rẻ) chạy `make asan`. Đặt nó cùng
   nhóm với `core-tests` để tín hiệu pass/fail độc lập với bản dựng artifact.

5. Chỉ bật `-Werror` **trong CI**, không bật ở bản dựng local (tránh chặn dev vì một cảnh
   báo mới của trình dịch mới).

---

### ⬜ C2 — Không fuzz tầng parse

**Hiện trạng.** `core/src/wire/Wire.cpp` là ranh giới tin cậy của toàn bộ chương trình —
chính header của nó nói vậy ở dòng 22-23. Bài test hiện có
(`WireTests.cpp:516`) dùng xorshift32 có hạt giống cố định: bản năng đúng, nhưng vẫn chỉ là
~300 vector tĩnh.

**Cách sửa.** Một target libFuzzer ~30 dòng là đủ và là mức chuẩn của ngành cho parser mạng:

```cpp
// core/fuzz/WireFuzz.cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n) {
    std::span<const uint8_t> pkt(d, n);
    const auto h = deskhub::ParseCommonHeader(pkt);
    if (!h) return 0;
    const auto p = deskhub::PayloadOf(pkt);
    // dispatch theo h->type qua TẤT CẢ các Parse* — giống hệt switch của HostSession
    ...
}
```

Build sau preset `asan` (§C1), chạy 60 giây mỗi lần CI với corpus commit vào repo. Ưu tiên
phủ: `ParseSourceList`, `ParseInputEvents`, `ParseNack`, `ParseAnnounce`, `ParseClipboardChunk`
— tất cả đều đọc một trường đếm do bên kia khai.

Giai đoạn 2 (không gấp): fuzz cả **máy trạng thái**, không chỉ parser — bơm chuỗi datagram
ngẫu nhiên vào `HostSession::HandlePacket` + `Tick`.

---

### ⬜ C3 — Coverage đo nhưng không có ngưỡng

`.github/workflows/build.yml` chạy `make coverage` và đẩy HTML lên artifact, nhưng không có
gì fail khi phủ tụt (comment trong workflow đã tự ghi nhận điều này). Thêm một ngưỡng sàn cho
`core/src` — đọc từ `llvm-cov report`, so với một con số commit trong repo. Đặt ngưỡng bằng
mức hiện tại làm mốc, chỉ cho phép đi lên.

---

### ⬜ C4 — Không có `.clang-tidy`

Phần style thực sự trên mức trung bình: clang-format / ktlint / swiftformat / swiftlint đều
**pin phiên bản** và được CI check bằng **đúng script dev chạy local** — chỗ này không cần
động vào.

Thiếu là phân tích tĩnh cho những thứ format không thấy. Thêm `.clang-tidy` ở gốc với
`bugprone-*`, `cert-*`, `cppcoreguidelines-narrowing-conversions`, `performance-*`. Chạy trên
`core/` trước (nhỏ, sạch nhất), mở rộng sang `client/` sau. Đã có
`CMAKE_EXPORT_COMPILE_COMMANDS: ON` trong preset nên không cần thêm hạ tầng gì.

---

## 4. Kiến trúc & code chết

### ⬜ D1 — Toàn bộ hệ đo trễ e2e trong core không ai dùng; các client tự viết lại

**Hiện trạng.** Đã grep toàn bộ `client/` (mọi `.cpp/.h/.mm/.cs/.kt/.swift`):

| Module | Kích thước | Người dùng ngoài core |
|--------|-----------|------------------------|
| `control/ClockSync.{h,cpp}` | 102 + 62 dòng | **không ai** |
| `control/LatencyTrace.{h,cpp}` | 82 + 99 dòng | **không ai** |
| `LinkStats::AddE2e` | — | **không client nào gọi** |

Hệ quả dây chuyền: `LinkWindow::e2eMsAvg` / `e2eMsMax` / `e2eSamples`
(`LinkStats.cpp:67-71`) **vĩnh viễn bằng 0** ở mọi client.

Trong khi đó `client/android/app/src/main/cpp/ClientLoop.cpp:335-343` **tự cài lại** bộ ước
lượng offset ("e2e = bây giờ − offset − pts của frame", kèm minRTT riêng), và iOS/macOS/
Windows cũng làm tương tự trong ClientLoop của mình.

**Vì sao là vấn đề.** Đây đúng là sự trùng lặp mà `core/` sinh ra để ngăn — và trớ trêu là
bản **không** được test lại chính là bản đang chạy thật trên tay người dùng, còn bản đã test
kỹ thì nằm im. Giữ code-đã-test-nhưng-chết cạnh code-chưa-test-nhưng-sống là tệ hơn cả hai
lựa chọn dứt khoát.

**Cách sửa — chọn dứt khoát một hướng:**

- **(a) Nối client vào core.** Thay phần tính offset trong 4 ClientLoop bằng
  `ClockSync::OnFrame` / `E2eUs`, và feed `LinkStats::AddE2e`. Đúng nguyên tắc kiến trúc,
  và `docs/09-diagnostics.md` mô tả hệ này như thể nó đang chạy. Công lớn hơn nhưng trả
  đúng món nợ.
- **(b) Xoá `ClockSync` + `LatencyTrace` + nửa e2e của `LinkStats`.** Cùng các ca test của
  chúng trong `ControlTests.cpp`, và cập nhật `09-diagnostics.md`. Trung thực với hiện
  trạng, và loại luôn chỗ treo B2 ở `LatencyTrace.cpp:28`.

Đề xuất **(a)** nếu biểu đồ độ trễ vẫn nằm trong kế hoạch giao diện; **(b)** nếu không.
Không chọn thì mặc định là đang chọn phương án tệ nhất.

---

### ⬜ D2 — Discovery chỉ có trên Windows *(ghi nhận, không phải lỗi)*

`Beacon` / `HostRegistry` chỉ được `client/windows` tham chiếu. macOS/Android/iOS không dò
được host và cũng không bị dò thấy. Ổn với trạng thái triển khai hiện tại — nhưng các header
trong `core/include/deskhub/discovery/` đọc như thể tính năng này phổ quát. Thêm một dòng
"nền tảng đã nối: Windows" vào đầu `Beacon.h` và `HostRegistry.h` cho khớp sự thật.

---

### ⬜ D3 — `platform/Clock.h` rò rỉ

Bốn vấn đề trong một file 111 dòng:

1. **Macro không guard.** `platform/include/deskhubp/Clock.h:69-70`:
   ```cpp
   #define WIN32_LEAN_AND_MEAN
   #define NOMINMAX
   ```
   TU nào đã định nghĩa sẵn (rất phổ biến — hầu hết file trong `client/windows/cpp` đều có)
   sẽ ăn **C4005 macro redefinition**. Core đang build ở `/W4`; thêm `/WX` (§C1) là gãy build.
   Dạng chuẩn: `#ifndef X` / `#define X` / `#endif`.

2. **`NowUs()` ở global namespace.** Đường include là `deskhubp/Clock.h`, CMake đặt tên
   library là `platform`, nhưng hàm không nằm trong `namespace deskhubp`. Một public header
   xuất một global tên `NowUs` là va chạm tên chờ sẵn. Đưa vào `namespace deskhubp` và sửa
   call site (grep `NowUs()` trong `client/`).

3. **`<windows.h>` vào mọi consumer.** Chính comment của file (`Clock.h:62-63`) phê phán việc
   kéo trọn `windows.h`, rồi vẫn làm đúng thế với mọi TU include nó. Chuyển `platform` thành
   **STATIC** + `Clock.cpp` — `platform/CMakeLists.txt:6-8` đã dự liệu sẵn bước này. Giá phải
   trả là một lời gọi không inline mỗi vòng lặp mạng: không đáng kể cạnh một `sendto`.

4. **`QueryPerformanceFrequency` không kiểm tra giá trị trả về** (`Clock.h:77`). Thất bại →
   `freq` chưa khởi tạo → chia cho rác hoặc cho 0. Một câu `if` là xong.

---

## Thứ tự triển khai đề xuất

1. **§3 C1** — cờ cảnh báo + preset ASan + job CI. Rẻ nhất, và nó tự bắt giúp phần còn lại.
2. **§4 D3** — `Clock.h`. Nhỏ, và mục (1) của nó là điều kiện để bật `/WX` ở bước trên.
3. **§2 B1, B2, B3** — ba lỗi độc lập, mỗi cái một commit + một ca test.
4. **§1 A2, A3** — bảo mật không cần chốt thiết kế, làm được ngay.
5. **§4 D1** — chốt (a) hay (b) rồi làm.
6. **§1 A1** — chốt (a) hay (b) rồi làm. Cần đụng UI của cả 4 nền tảng nên để sau cùng.
7. **§3 C2, C3, C4** — nâng dần khi các mục trên đã xanh.

---

## Đã làm (2026-07-26, lần 2)

Triển khai theo thiết kế Claude Design *"Deskhub App"* — màn `06 · settings / password +
trusted devices` (desktop) và `05 · settings / password` (mobile). Thiết kế chốt cơ chế
**challenge-response**, không phải gửi mật khẩu: *"kept in the keychain — never sent. the
client answers a challenge, the password stays on this Mac."*

**Mới:**

| File | Vai trò |
|------|---------|
| `core/include/deskhub/crypto/Sha256.h` + `src/crypto/Sha256.cpp` | SHA-256, HMAC, PBKDF2, so hằng thời gian — thuần C++20 |
| `core/include/deskhub/auth/PasswordAuth.h` + `src/auth/PasswordAuth.cpp` | Phép toán bắt tay, không trạng thái |
| `core/include/deskhub/auth/AuthGuard.h` + `src/auth/AuthGuard.cpp` | Mật khẩu, khoá tạm 3/5 phút, thiết bị tin cậy |
| `platform/include/deskhubp/Random.h` | CSPRNG: BCrypt / arc4random / getrandom |
| `core/tests/crypto/CryptoTests.cpp` | Vector chuẩn FIPS 180-4, RFC 4231, RFC 7914 |
| `core/tests/auth/AuthTests.cpp` | 12 nhóm ca, phần lớn khẳng định thứ gì đó **bị từ chối** |

**Sửa:** `Wire.{h,cpp}` (AUTH_CHALLENGE 0x09 / AUTH_RESPONSE 0x0A, `RejectReason`, trường
nối đuôi cho HELLO/HELLO_ACK), `HostSession.{h,cpp}` (trạng thái `Authenticating`,
`InSession`, `BeginSession`, `GrantInput`), `ClientSession.{h,cpp}` (`SetPassword`,
`AnswerChallenge`, thông báo từ chối theo lý do), `AgentLoop.cpp` ×2 (nối `randomBytes`),
`Clock.h` (guard macro + kiểm tra QPF), `docs/04-protocol.md` §7b.

**Vì sao tự cài SHA-256 thay vì link thư viện:** mỗi nền tảng một thư viện khác nhau
(BCrypt / CommonCrypto / Android NDK không có sẵn) → bốn nhánh `#ifdef` ngay giữa lõi, đúng
thứ `core/` sinh ra để tránh. SHA-256 là thuật toán cố định, ~150 dòng, kiểm chứng được
bằng vector công bố. Entropy thì **không** tự cài được — nó là thứ duy nhất nằm ở
`platform/`.

**Quyết định đáng chú ý — fail closed.** Không nối `HostCallbacks::randomBytes` thì host
**từ chối mọi kết nối** thay vì lùi về `sessionId` dẫn từ đồng hồ. Lùi sẽ dựng lại đúng
điểm yếu A3 vừa bỏ đi, ở một đường hiếm khi chạy nên không ai thử tới. Hệ quả: mọi test
dựng `HostSession` phải nối `TestRandomBytes` (đã sửa), và mọi host thật phải nối
`deskhubp::RandomBytes` (đã nối Windows + macOS).

### Còn lại của A1 — giao diện + keychain

Phần core đã xong và có test. **iOS + Android (vai client) đã xong** — xem mục riêng bên
dưới. Còn lại:

- ⬜ **Windows (vai host)** — màn Settings (WinUI3) theo `DesktopSettings`: ô require,
  trường mật khẩu + xác nhận + Generate, cặp số `0/3 · 5 min`, danh sách thiết bị tin cậy
  + Forget. Lưu `AuthKey` + danh sách vào DPAPI/Credential Manager qua
  `onTrustedDevicesChanged`.
- ⬜ **macOS (vai host)** — cùng màn, lưu bằng Keychain Services.
- ⬜ **Windows/macOS (vai client)** — ô nhập mật khẩu khi `onPasswordNeeded` bắn; lưu token
  qua `onDeviceToken`. Mẫu đã có sẵn ở `client/ios` và `client/android`, port thẳng.
- ⬜ **Hộp thoại duyệt input** — `SetAskBeforeInput(true)` đã giữ phiên ở chế độ chỉ-xem;
  còn thiếu cái hỏi và nút gọi `GrantInput()`.

### iOS + Android — ✅ xong (2026-07-26, lần 3)

Cả hai là **client-only** theo thiết kế (`phoneIsClient` / `hostingDesktopOnly`), nên phần
làm là đầu client của bắt tay. Mặt tiền C++ giống hệt nhau ở hai nền tảng:
`ClientLoop::Credentials` (clientId / deviceName / password / deviceToken),
`Phase::NeedPassword`, `SubmitPassword`, `TakeDeviceToken`, `rejectReason`.

| | iOS | Android |
|---|---|---|
| Kho bí mật | `Credentials.swift` — Keychain, `kSecAttrAccessibleWhenUnlockedThisDeviceOnly` | `Credentials.kt` — Android Keystore + AES/GCM |
| Bridge | `dh_start`(+4 tham số), `dh_submit_password`, `dh_take_device_token`, `dh_reject_reason` | `nativeStart`(+4), `nativeSubmitPassword`, `nativeTakeDeviceToken`, `nativeRejectReason` |
| Ô nhập mật khẩu | `StreamView.passwordOverlay` | `StreamActivity.PasswordOverlay` |
| Saved passwords | `ConnectView.savedPasswordSection` | `MainActivity` (dưới Recents) |
| Sinh trắc học | Face ID qua `LocalAuthentication` ✅ | ⬜ chưa (xem bên dưới) |

**Lỗi đã bắt được khi nối dây:** `hello.clientId = uint32_t(NowUs())` — clientId đổi mỗi
lần chạy, mà `AuthGuard` khoá danh sách thiết bị tin cậy **theo clientId**. Token sẽ không
bao giờ khớp lại và người dùng phải gõ mật khẩu mãi mãi. Giờ nó là một giá trị ngẫu nhiên
sinh một lần rồi cất vào Keychain/Keystore; lùi về đồng hồ nếu caller quên truyền (mất tính
năng nhớ thiết bị, vẫn kết nối được).

**Vì sao Android không dùng `androidx.security:security-crypto`:** `EncryptedSharedPreferences`
đã bị Google đánh dấu lỗi thời (2024). Thêm một dependency đã hết bảo trì chỉ là dời khoản
nợ sang chỗ khác; Keystore + AES/GCM là API nền tảng và gọn hơn phần cấu hình thư viện kia đòi.

**Chưa làm:** sinh trắc học trên Android (`BiometricPrompt` cần thêm dependency
`androidx.biometric` + một Activity host). iOS đã có Face ID vì `LocalAuthentication` chỉ
tốn ~25 dòng và không cần dependency mới.

⚠ **iOS chưa được biên dịch kiểm chứng** — máy dev là Windows, Xcode không chạy được ở đây.
Android (C++/JNI/Kotlin) và core đã build xanh tại chỗ; iOS chỉ mới qua `swiftformat --lint`
(0/21 file cần sửa). Job `ios` của `.github/workflows/build.yml` (`make build-ios` trên
macos-latest) là nơi bắt lỗi biên dịch Swift/ObjC++ đầu tiên.

### Chưa làm, có chủ ý

- **Mã hoá luồng** vẫn là GĐ6 — xác thực và mã hoá là hai việc khác nhau, và `04-protocol.md`
  §7b nói rõ điều đó ở chỗ dễ thấy nhất.
- **`AuthGuard` giữ một challenge dở dang** (v1 một client mỗi phiên). Cũng chặn luôn việc
  mở hàng nghìn challenge song song để bào bộ nhớ host.
- **PBKDF2 chạy trên thread mạng của client** (~100 ms lúc bắt tay, chưa có video). Khoá dẫn
  xuất được cache theo `(salt, iterations)` nên các lần HELLO phát lại không tính lại.

## Liên quan

- `01-architecture.md` §Bảo mật — nơi ghi khoản hoãn mã hoá (DTLS/AEAD)
- `04-protocol.md` — đặc tả wire, phải sửa cùng lúc với A1/B1/B3
- `05-roadmap.md` GĐ6 — nơi cần bổ sung mục **uỷ quyền** (hiện chỉ có mã hoá)
- `06-phase3-transport.md` — mô tả FEC, phải sửa cùng B3
- `09-diagnostics.md` — mô tả hệ e2e, phải sửa cùng D1
