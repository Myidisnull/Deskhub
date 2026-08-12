# TODO — Deskhub 4.0: chia sẻ màn hình từ iOS / Android (mobile host)

> File làm việc cá nhân, không thuộc bộ tài liệu song ngữ. Xóa khi hoàn thành.
>
> **Phát hành dưới tên Deskhub 4.0.** Hiện tại là `3.0.1`. Đây là major bump hợp lý:
> tính năng thay đổi vai trò của cả một lớp thiết bị (mobile từ view-only thành host),
> kéo theo PRIVACY version mới và bảng capability mới — đúng nghĩa breaking về mặt mô tả
> sản phẩm, dù wire protocol không đổi (`kProtocolVersion = 1` giữ nguyên, client cũ vẫn
> nói chuyện được với host mới).
>
> **Bối cảnh:** toàn bộ phần dùng chung đã có sẵn và đã biên dịch trên cả 2 mobile target:
> giao thức + packetizer + FEC (`core/`), máy trạng thái host (`core/include/deskhub/session/HostSession.h`,
> `HostRouter.h`), passcode gate, bitrate thích ứng, và FFI host hoàn chỉnh
> (`platform/include/deskhubp/ffi/AgentSession.h` — `dha_start`, `dha_stop`, `dha_host_rows`…).
> Chỉ thiếu 3 mảnh per-OS: **capture**, **encoder**, và **AgentLoop** nối chúng lại — cộng UI + quyền hệ thống.
>
> **Mẫu chuẩn để nhìn theo:** `client/macos/app/cpp/AgentLoop.cpp` (~200 dòng) — cách điền
> ~10 lambda vào `HostEnginePolicy` (`platform/include/deskhubp/session/HostEngine.h:31-48`).
> Dòng chảy frame: capture callback → `AdmitCapturedFrame` (core quyết định drop/rebuild/resize)
> → `encoder->Encode(handle, tsUs, forceIdr)` → `onPacket` → `Packetizer` → UDP tới viewer.

---

## Giai đoạn 0 — Chuẩn bị chung (làm trước, hưởng lợi cả 2 OS)

- [ ] **Nâng `VtEncoder` từ `client/macos/app/cpp/encode/` lên `platform/`**
  (`platform/src/media/`, header trong `platform/include/deskhubp/media/`), cho macOS dùng
  qua đường mới, xong mới bắt đầu iOS.
  - ⚠️ `VtDecoder` đã được nâng lên `platform/` rồi (`VtDecoderApple.mm`) — encoder là chỗ
    bất đối xứng còn sót. Rule CLAUDE.md: không bao giờ duplicate logic giữa các `client/*`.
  - ⚠️ Sau khi chuyển, chạy đủ `make build-macos` để chắc macOS không gãy — đây là refactor
    thuần, không đổi hành vi.
- [ ] Đọc kỹ 2 contract phải thỏa (đều là C++20 concepts, kiểm tra lúc compile, không phải
  interface ảo):
  - `core/include/deskhub/media/VideoContract.h` — `VideoEncoderLike<E, Frame>`:
    `Encode(Frame, tsUs, forceKeyframe)`, `SetBitrate(bps)`, `Finish()`, `BackendName()`.
    `Frame` tự chọn kiểu handle native (macOS dùng `CVPixelBufferRef`, Windows dùng
    `ID3D11Texture2D*`).
  - `core/include/deskhub/media/CaptureContract.h` — `ScreenCaptureLike`: `Stop()`,
    `Closed()`; kèm entry point ngầm định mọi platform đều có:
    `bool Start(uint64_t targetId, const CaptureOptions&, FrameHandler onFrame)`.
    Nên thỏa thêm `QualityAwareCapture` (`SetQuality`) để bitrate ladder hạ độ phân giải được.

---

## Giai đoạn 1 — Android (làm trước vì dễ hơn: 1 process, đường chuẩn của Google)

### 1a. Native (C++ / NDK) — trong `client/android/app/src/main/cpp/`

- [ ] **Encoder `MediaCodecEncoder`** (`encode/MediaCodecEncoder.{h,cpp}`) — soi gương
  `decode/MediaCodecDecoder.{h,cpp}` đã có. Dùng NDK `AMediaCodec` H.264, **input là
  `ANativeWindow*` (Surface mode)** — `AMediaCodec_createInputSurface`.
  - ⚠️ Codec toàn hệ thống là H.264 duy nhất (`kCodecMaskH264` trong
    `core/include/deskhub/protocol/Wire.h`) — đừng thêm HEVC/AV1.
  - ⚠️ Output của MediaCodec có thể là AVCC hoặc thiếu SPS/PPS inline — pipeline cần Annex-B
    và IDR phải kèm SPS/PPS (xem `core/include/deskhub/media/AnnexB.h` và cách `VtEncoder`
    chèn parameter set trước IDR). Set `KEY_PREPEND_HEADER_TO_SYNC_FRAMES` nếu API level cho
    phép, không thì tự chèn từ `csd-0`/`csd-1`.
  - ⚠️ `SetBitrate` lúc đang chạy: dùng `PARAMETER_KEY_VIDEO_BITRATE`
    (`AMediaCodec_setParameters`). `forceKeyframe`: `PARAMETER_KEY_REQUEST_SYNC_FRAME`.
  - ⚠️ Kích thước encode phải qua `deskhub::AlignEncodeSize`
    (`core/include/deskhub/media/H264Encode.h`) — MediaCodec kén width/height không chia
    hết cho 16.
- [ ] **Capture `ScreenCapture`** (`capture/ScreenCapture.{h,cpp}`) — nhận `MediaProjection`
  từ phía Kotlin, tạo `VirtualDisplay` xuất thẳng vào **input Surface của encoder**.
  - ⚠️ Đây là điểm ăn tiền của Android: capture → encode nối trực tiếp qua Surface,
    zero-copy, không cần đọc pixel về CPU. Nhưng nó làm mô hình lệch với desktop
    (desktop: capture đưa frame handle → AgentLoop gọi `Encode`). Hai hướng:
    1. Giữ Surface-to-Surface: `Encode()` gần như no-op, frame tự chảy; AgentLoop chỉ còn
       quản lý vòng đời + bitrate. Ít code, nhưng `AdmitCapturedFrame` (đếm frame, quyết
       định resize) cần được gọi từ output callback của encoder thay vì capture callback.
    2. Dùng `ImageReader` lấy buffer về rồi đưa vào encoder theo đúng mẫu desktop — khớp
       kiến trúc hơn nhưng tốn 1 lần copy.
    → Chọn hướng 1, nhưng viết sao cho `AgentLoop.cpp` của Android vẫn gọi đủ
    `AdmitCapturedFrame`/`MakeEncoderConfig` để logic chất lượng trong core hoạt động.
  - ⚠️ Đổi độ phân giải giữa chừng (quality ladder hạ bậc): `VirtualDisplay.resize()` +
    encoder phải rebuild (MediaCodec không đổi size nóng được) — `FrameAdmission.rebuildEncoder`
    từ core đã báo sẵn thời điểm.
- [ ] **`DisplayEnumAndroid.cpp`** trong `platform/src/media/` thay stub `DisplayEnumNone.cpp`,
  và sửa mục chọn file per-OS trong `platform/CMakeLists.txt` (khoảng dòng 19–63).
  - ⚠️ Điện thoại chỉ có 1 màn hình → trả về đúng 1 `ShareSource`
    (`core/include/deskhub/media/ShareSource.h`) với kích thước thật của display.
    Máy móc multi-display (`kMaxSources = 8`) cứ để nguyên, không dùng đến.
  - ⚠️ Lấy kích thước màn hình cần Java side → truyền số qua JNI lúc init thay vì gọi
    ngược từ C++.
- [ ] **`AgentLoop.cpp` cho Android** — định nghĩa `AgentLoop::Start` (khai báo sẵn trong
  `platform/include/deskhubp/session/AgentLoop.h`). Copy khung từ macOS, thay
  capture/encoder bằng bản Android.
  - ⚠️ Lý do hiện tại link được dù thiếu symbol: `libplatform.a` là static archive, không ai
    gọi `dha_*` nên object chứa `AgentLoop::Start` không bị kéo vào. Thêm file này là mạch
    tự thông — không cần sửa gì thêm ở tầng session.
  - ⚠️ `attachInput`/`applyInput` trong `HostSourcePolicy`: **để rỗng / từ chối input**.
    Điều khiển ngược điện thoại đòi AccessibilityService — ngoài phạm vi. Truyền
    `allowInput = false` cố định từ UI.
- [ ] Thêm file mới vào `client/android/app/src/main/cpp/CMakeLists.txt`.

### 1b. Kotlin / manifest — trong `client/android/app/src/main/`

- [ ] **Foreground service** loại `mediaProjection`:
  - Manifest hiện chỉ có `INTERNET` + `ACCESS_NETWORK_STATE`. Phải thêm:
    `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_MEDIA_PROJECTION`, `POST_NOTIFICATIONS`
    (Android 13+ để hiện notification của service), và khai báo
    `<service android:foregroundServiceType="mediaProjection">`.
  - ⚠️ targetSdk 36: **bắt buộc** gọi `startForeground()` với đúng type *trước khi* dùng
    `MediaProjection`, nếu không hệ thống ném `SecurityException`. Thứ tự:
    xin consent → start service → `startForeground` → tạo `MediaProjection` → gọi native.
  - ⚠️ Token consent của `MediaProjection` **dùng một lần** — mỗi lần bấm Start sharing là
    một lần dialog hệ thống, không cache được (từ Android 14 user còn có thể chọn chia sẻ
    1 app thay vì cả màn hình — cứ nhận cái gì hệ thống đưa).
  - ⚠️ Đăng ký `MediaProjection.Callback.onStop()` — user có thể tắt sharing từ status bar
    hệ thống; phải gọi `dha_stop` và cập nhật UI, đừng để session treo.
- [ ] **JNI**: thêm các hàm `native*` trong `NativeClient.kt` + `JniBridge.cpp` bọc
  `dha_start`/`dha_stop`/`dha_running`/`dha_host_rows`/`dha_last_error`, và một hàm truyền
  `MediaProjection` (dạng Surface/VirtualDisplay setup) xuống native.
- [ ] **Tab Host**: thêm `HOST` vào `private enum class Section` trong `MainActivity.kt`
  (hiện chỉ `CLIENT, SETTINGS`, TabRow ở ~dòng 400) và màn hình Host: nút Start/Stop,
  hiện passcode + địa chỉ IP (`dha_local_addresses`), danh sách viewer (`dha_host_rows`),
  nút kick (`dha_kick_viewer`).
  - ⚠️ Chuỗi UI có sẵn hết rồi: `DHStrSidebarHost`, `DHStrStartSharing`, `DHStrSharingTitle`,
    `DHStrShareStateOn/Off`… trong `platform/include/deskhubp/ffi/ClientFfi.h:73-134` —
    dùng qua FFI, đừng hardcode chuỗi mới.
  - ⚠️ Settings mobile đang load/save đủ `fps/bitrateMbps/maxDim/passcode` trong
    `DHUiSettings` nhưng không hiển thị — giờ cần surface chúng (ít nhất là passcode)
    trong SettingsView.

### 1c. Kiểm tra Android

- [ ] Host từ điện thoại → xem từ Linux/macOS client: hình chạy, passcode sai bị chặn
  (3 lần → lockout 30s), điện thoại **tự xuất hiện** trong scan của desktop (discovery là
  client quét subnet + gõ port 47777 — phone listen là tự hiện, không cần code thêm).
- [ ] App xuống background / tắt màn hình khi đang share → service sống, stream tiếp.
- [ ] Xoay màn hình → kích thước đổi → `AdmitCapturedFrame` báo rebuild → viewer nhận
  kích thước mới không vỡ hình.
- [ ] Doze/battery optimization không giết service giữa chừng (test share > 10 phút).

---

## Giai đoạn 2 — iOS (khó hơn: Broadcast Upload Extension chạy khác process)

### 2a. Quyết định kiến trúc TRƯỚC KHI code — điểm khó nhất toàn dự án

- ⚠️ Muốn quay **toàn bộ màn hình** (kể cả ngoài app) Apple bắt buộc dùng
  **Broadcast Upload Extension** — một target riêng, chạy trong **process riêng**, nhận
  frame qua `SampleHandler.processSampleBuffer` (`CMSampleBuffer`). Còn `RPScreenRecorder`
  in-app chỉ quay được nội dung bên trong app Deskhub — vô nghĩa với use case này.
- ⚠️ Extension bị giới hạn RAM ngặt (~50 MB) và không chia sẻ process với app → socket UDP,
  `HostSession`, encoder phải nằm ở đâu? Hai phương án:
  1. **Chạy cả pipeline host trong extension** (khuyên dùng): extension link `core` +
     `platform`, tự encode (VideoToolbox) + tự chạy `dha_*`. App chính chỉ còn là bảng
     điều khiển đọc trạng thái qua App Group (UserDefaults/file). Ưu: không phải chuyển
     frame liên process. Nhược: extension phải sống tiết kiệm RAM — VideoToolbox +
     packetizer ước tính vẫn lọt 50 MB nếu không giữ nhiều frame.
  2. Chuyển frame từ extension sang app qua shared memory / socket cục bộ: phức tạp hơn
     hẳn, thêm 1 lần copy, chỉ đáng nếu phương án 1 vỡ RAM. Đừng bắt đầu bằng phương án này.
- ⚠️ Cần **App Group** (entitlement cả 2 target) để chia sẻ settings/passcode/trạng thái
  giữa app và extension — nghĩa là đụng vào provisioning/signing, làm trên máy có tài
  khoản Apple Developer.

### 2b. Việc cụ thể

- [ ] Tạo target **Broadcast Upload Extension** trong `client/ios/Deskhub.xcodeproj`
  (+ target Broadcast Setup UI nếu muốn nút picker đẹp — `RPSystemBroadcastPickerView`).
  - ⚠️ Xcode project hiện build native bằng Run Script phase gọi CMake
    (`-DCMAKE_SYSTEM_NAME=iOS`) rồi link `-lplatform -lcore` — extension target cần lặp
    lại đúng cơ chế đó.
- [ ] `SampleHandler` (Swift): nhận `CMSampleBuffer` video → lấy `CVPixelBufferRef` →
  đẩy xuống C qua FFI. Frame audio bỏ qua (Deskhub chưa có audio).
- [ ] **Capture shim C++** phía native: không có "capture API" thật — extension *được đưa*
  frame. Viết một `ScreenCapture` giả thỏa `ScreenCaptureLike` mà `Start()` chỉ đăng ký
  callback, còn frame do FFI bơm vào. `Stop()`/`Closed()` map sang
  `broadcastFinished`.
- [ ] **`AgentLoop.cpp` cho iOS** — dùng `VtEncoder` đã nâng lên `platform/` ở Giai đoạn 0,
  handle là `CVPixelBufferRef` (`void*`) y hệt macOS. Phần này gần như copy macOS.
- [ ] **`DisplayEnumIos.cpp`** — 1 màn hình, kích thước từ `UIScreen` truyền qua FFI,
  sửa `platform/CMakeLists.txt`.
- [ ] App chính: nối route `.sharing` có sẵn (`client/apple/swift/ClientRoute.swift` đã có
  `case sharing`; `ContentView.swift` của iOS đang render `EmptyView()`) → view Host với
  `RPSystemBroadcastPickerView` + trạng thái đọc từ App Group.
  - ⚠️ Bridging header iOS (`client/ios/app/swift/Deskhub-Bridging-Header.h`) hiện chỉ
    import `ClientFfi.h`, `ClientSession.h`, `DiscoveryFfi.h` — thêm `AgentSession.h`
    (macOS đã import tương đương qua `DeskhubBridge.h`).
- [ ] Input injection: **không làm** trên iOS (OS không cho phép app inject input toàn cục).
  `allowInput = false` cố định.

### 2c. Kiểm tra iOS

- [ ] Share từ iPhone → xem từ desktop; thoát app chính giữa chừng → extension vẫn stream.
- [ ] Theo dõi RAM extension (Instruments) khi stream 1080p — phải cách xa trần 50 MB.
- [ ] Cuộc gọi đến / notification full-screen → hệ thống dừng broadcast →
  `broadcastFinished` dọn session sạch sẽ.

---

## Giai đoạn 3 — Tài liệu + dọn dẹp (bắt buộc, CLAUDE.md coi lệch .vi.md là bug)

- [ ] `README.md` + `README.vi.md`: bảng Platforms (Android/iOS từ `—` sang `✅` cột Host),
  câu "phones and tablets drop **Host**" (~dòng 35).
- [ ] `docs/SPECIFICATION.md` + `.vi.md`: bảng "Roles by platform" (dòng 37–51),
  mục **P-4** dòng 169 ("View and control only — these devices cannot share…"),
  mục **D-1** dòng 85 ("phones and tablets never appear at all" — sai ngay khi mobile
  host tồn tại). Spec chỉ mô tả hành vi, không viết chi tiết implementation.
- [ ] `PRIVACY.md` + `.vi.md`: thay đổi này đụng "cái gì được truyền đi" → **bắt buộc**
  version mới + effective date mới + dòng changelog, cả 2 ngôn ngữ, cùng 1 commit.
- [ ] `SECURITY.md` + `.vi.md`: threat model thêm trường hợp điện thoại làm host
  (passcode gate áp dụng y nguyên).
- [ ] Store listing `fastlane/metadata/*/vi/` nếu mô tả app nhắc "chỉ xem".

---

## Giai đoạn 4 — Phát hành Deskhub 4.0

- [ ] Đổi `VERSION` từ `3.0.1` → `4.0.0` — đây là nguồn version duy nhất: CMake đọc nó,
  Android nhận qua property `versionName` (build.gradle.kts fallback `0.1-dev` chỉ là giá
  trị dev), iOS/fastlane và các script build-deb/build-rpm đều lấy từ đây.
  - ⚠️ CI (`scripts/check-version.sh`, chạy trong workflow deploy) bắt buộc tag git phải
    là `v4.0.0` khớp đúng nội dung file `VERSION` — lệch là fail. Quy trình: commit đổi
    `VERSION` trước, tag `v4.0.0` sau, rồi mới push tag.
  - ⚠️ Không đụng `kProtocolVersion` trong `core/include/deskhub/protocol/Wire.h` — wire
    protocol không đổi, client 3.x kết nối host 4.0 (và ngược lại) phải vẫn chạy. Chỉ tăng
    protocol version khi format message thật sự đổi.
- [ ] Kiểm tra tương thích chéo trước khi tag: client 3.0.1 (desktop cũ chưa update) xem
  được stream từ điện thoại chạy 4.0, và mobile 4.0 vẫn xem được host desktop 3.0.1.
- [ ] Build release đủ 5 nền tảng: `make release-linux/-windows/-macos/-ios/-android`.
- [ ] Android: version dialog xin quyền MediaProjection + foreground service là hành vi
  mới với store review — cập nhật mô tả app và data-safety form trên Play Console cho
  khớp PRIVACY 4.0.
- [ ] iOS: App Store review với Broadcast Upload Extension — mô tả rõ tính năng screen
  sharing trong review notes; App Group + entitlement mới phải có trong provisioning
  profile release.

---

## Quy tắc xuyên suốt (đọc lại trước mỗi PR)

- **Không viết comment trong code** — kể cả C++ mới, Kotlin, Swift, `make/*.mk`. Tên hàm
  tự giải thích. (Ngoại lệ: Makefile gốc, CMakeLists.txt, scripts, workflows.)
- **Reuse trước, viết sau**: cái gì platform-agnostic → `core/` (kèm test trong
  `core/tests/`); cái gì cần OS nhưng API giống nhau mọi nơi → `platform/`; chỉ phần
  thật sự OS-specific mới nằm `client/<os>/`. Không bao giờ chép logic giữa 2 client.
- `core/` cấm include header OS/third-party, cấm phụ thuộc `platform/`.
- Log bằng `LOGI/LOGW/LOGE` từ `deskhubp/Log.h`, không `printf`/`Log.d` tự chế ở tầng native.
- Trước khi coi là xong: `make test` + `make lint`. CI còn gate thêm: clang-tidy trên
  `core/src` + `platform/src` (code mới trong `platform/` sẽ bị soi), SwiftLint `--strict`,
  Android Lint, coverage core ≥ 90% lines / 80% branches — nếu có thêm logic vào `core/`
  thì phải kèm test đủ độ phủ.
- Naming: `PascalCase` hàm/kiểu, `camelCase` biến cục bộ, hậu tố `_` cho member private.
  Format bằng `make format`, không format tay.
