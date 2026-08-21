# Todo — gỡ trùng lặp & đưa code về `core/` / `platform/`

Kết quả rà soát ngày 2026-08-21 trên toàn bộ `core/`, `platform/`, `client/*`.
Ước lượng khoảng **550–600 dòng** trùng có thể bỏ.

Thứ tự đề xuất: **1 → 5 → 2 → 4 → 3**, rồi tới Tier 2.
(Mục 1 rủi ro thấp nhất và test được ngay trong `core`; mục 3 để sau vì phải chốt hành vi trước.)

Ghi chú: file này là ghi chú công việc, không thuộc bộ tài liệu song ngữ trong
`CLAUDE.md` §Documentation, nên không cần bản `.vi.md`.

---

## Tier 1 — trùng rõ, nên lift

### 1. Ring buffer PCM copy-paste 4 lần trong chính `platform/`

- [ ] Tạo `core/include/deskhub/media/PcmRing.h` + test `core/tests/media/PcmRingTests.cpp`
- [ ] Thay thế ở cả 4 backend
- [ ] Gói luôn pattern "đổ silence phần thiếu rồi tăng `starved`" thành `TakeOrSilence()`

Cùng một struct (`ringMutex` / `ring` / `readAt` / `filled` / `dropped` / `starved`
+ `Take()` + `Put()` + `kRingFrames`), giống nhau từng ký tự:

| File | Dòng |
| --- | --- |
| `platform/src/audio/AudioSinkPipewire.cpp` | 43 |
| `platform/src/audio/AudioSinkAAudio.cpp` | 32 |
| `platform/src/audio/AudioSinkWasapi.cpp` | 71 |
| `platform/src/audio/AudioSinkCoreAudio.mm` | 65 |

Thuần C++, không header OS → thuộc về `core/`. Đoạn đổ silence lặp lại trong cả 4
render callback. **~180 dòng.**

### 2. `AudioCapture` nằm sai tầng

- [ ] Thêm `platform/include/deskhubp/audio/AudioCapture.h` (namespace `deskhubp`)
- [ ] Chuyển thành `AudioCapturePipewire.cpp` / `AudioCaptureWasapi.cpp` / `AudioCaptureNone.cpp`
      trong `platform/src/audio/`, chọn theo OS trong `platform/CMakeLists.txt`
- [ ] Gộp boilerplate PipeWire dùng chung với `AudioSinkPipewire.cpp` thành helper `PwStream` nội bộ
- [ ] Xoá `client/linux/cpp/capture/AudioCapture.*` và `client/windows/cpp/capture/AudioCapture.*`
- [ ] Bỏ luôn lambda `startAudioCapture`/`stopAudioCapture` trùng ở AgentLoop linux + windows

`client/linux/cpp/capture/AudioCapture.{h,cpp}` và `client/windows/cpp/capture/AudioCapture.{h,cpp}`
có **API public giống hệt nhau** (chỉ khác accessor `framesPaddedWithSilence`), ở global
namespace, trong khi bản đối xứng `AudioSink` lại nằm ở `platform/`. Đúng vi phạm rule #2
trong `CLAUDE.md`.

Kèm theo: `AudioCapture.cpp` (linux) và `AudioSinkPipewire.cpp` dùng chung ~80 dòng boilerplate
PipeWire (thread loop, props, pod builder, connect, teardown, bảng `pw_stream_events`) — chỉ khác
`PW_DIRECTION_INPUT` / `PW_DIRECTION_OUTPUT` và tag log.

### 3. `MainWindow.cpp` ↔ `MainFrame.cpp` — ~110 dòng điều phối host giống hệt

- [ ] **Chốt trước:** hành vi Linux hay Windows mới là đúng (xem divergence bên dưới)
- [ ] Tạo `deskhubp::HostShareController` giữ `agentLoop_` / `terminalHost_` / `fileHost_`
      / `shells_` / `transfers_`
- [ ] Inject phần toolkit qua `std::function`: `onError`, `postToUi`, `openLocalTerminal`,
      `onRowsChanged`
- [ ] Rút gọn cả `client/linux/gtk/MainWindow.cpp` lẫn `client/windows/win32/MainFrame.cpp`

Các hàm khác nhau **chỉ ở lời gọi message box và post-to-UI**:

| Hàm | Linux | Windows |
| --- | --- | --- |
| `DrainPairingRequests` | `MainWindow.cpp:974` | `MainFrame.cpp:1003` |
| `ApplySharingBanner` | `MainWindow.cpp:1763` | `MainFrame.cpp:1663` |
| `Sharing` | `MainWindow.cpp:1759` | `MainFrame.cpp:1241` |
| `Start/StopTerminalShare`, `StopTerminalRow` | `MainWindow.cpp:1774-1806` | `MainFrame.cpp:1578-1610` |
| `Start/StopFileShare`, `StopFilesRow` | `MainWindow.cpp:1808-1834` | `MainFrame.cpp:1612-1639` |
| `RefreshTransfers` / `RefreshShells` / `KickShell` / `StopAndAttachShell` | `MainWindow.cpp:1836-1857` | `MainFrame.cpp:1641-1661` |

> **Bản sao đã trôi lệch thật.** `StopTerminalRow()` / `StopFilesRow()` bên Linux kiểm tra
> `!screenSharing_ && !fileHost_.Running()`, bên Windows dùng `Sharing()` — mà `Sharing()`
> bao gồm cả `hosting_`, vốn vẫn `true` ở thời điểm đó. Kết quả: **Linux tắt hẳn host khi gỡ
> hàng cuối, Windows thì không.** Chính xác là loại bug mà việc gộp lại sẽ chặn được.

### 4. Pipeline VideoToolbox trùng giữa macOS và iOS

- [ ] Tạo `platform/include/deskhubp/media/VtSourcePipeline.h` (Apple-only, theo tiền lệ `VtEncoder.h`)
- [ ] Dùng lại ở `client/macos/app/cpp/AgentLoop.cpp` và `client/ios/broadcast/cpp/AgentLoop.cpp`

Khoảng **150/178 dòng** của bản iOS trùng bản macOS: `SourcePipeline` với `cachedPb` /
`ReleaseCached()` / `EncodeTimed()`, `ensureEncoderFn` khởi tạo `VtEncoder`, `onFrame`,
`policy.source.stopCapture`, `policy.source.flush`.

### 5. `TerminalFeed` / `RemoteFeed` / `LocalShellFeed` giống hệt từng byte

- [ ] Tạo `platform/include/deskhubp/session/TerminalFeed.h`
- [ ] Dùng lại ở cả hai `TerminalWindow.cpp`

`client/linux/gtk/TerminalWindow.cpp:78` và `client/windows/win32/TerminalWindow.cpp:78`,
~55 dòng, chỉ gọi `deskhubp::TerminalViewer` / `deskhubp::TerminalHost`, không đụng OS.

---

## Tier 2 — nhỏ hơn nhưng sạch

### 6. Thuật toán neo scrollback viết lại 4 lần

- [ ] Tạo `deskhub::term::ScrollAnchor` trong `core/` + test
- [ ] Áp dụng cho hai bản C++ (dùng được ngay)
- [ ] Swift / Kotlin: cần thêm một hàm FFI — tách thành việc riêng

"Khi có dòng mới về mà đang cuộn lên thì cộng bù offset":

| Client | Vị trí |
| --- | --- |
| Linux | `client/linux/gtk/TerminalWindow.cpp:288` |
| Windows | `client/windows/win32/TerminalWindow.cpp:167` |
| Apple | `client/apple/swift/TerminalModel.swift:270` |
| Android | `client/android/app/src/main/java/com/deskhub/app/TerminalActivity.kt:241` |

### 7. Định tuyến sau khi query xong — cùng bộ quy tắc ở cả 5 client

- [ ] Thêm `PlanAfterConnect(caps, sources, choice)` cạnh `DecideAfterSourceQuery`
      trong `core/include/deskhub/session/ConnectFlow.h`
- [ ] Áp dụng cho từng client

Quy tắc caps → mở shell / files / picker + thông báo lỗi tương ứng:

| Client | Vị trí |
| --- | --- |
| Linux | `client/linux/gtk/MainWindow.cpp` (`OnSourcesReady`) |
| Windows | `client/windows/win32/MainFrame.cpp:2056` |
| Android | `client/android/app/src/main/java/com/deskhub/app/MainActivity.kt:385` |
| iOS | `client/ios/app/swift/SessionModel.swift:86,111` |
| macOS | `client/macos/app/swift/ConnectView.swift:319` |

### 8. Trùng ngay bên trong `core/`

- [ ] Gom `Trim` + `ParseUnixTime` + `SanitizeName`/`SanitizeLabel` (chỉ khác hằng cap)
      vào một header nội bộ dùng chung

`core/src/net/PairedDevices.cpp:9-35` và `core/src/net/TrustStore.cpp:21-46`.

### 9. `Append(char*&, char*, fmt, ...)` giống hệt

- [ ] Đưa vào header dùng chung trong `core/include/deskhub/diag/`

`core/src/diag/AgentDiag.cpp:38` và `core/src/diag/ClientDiag.cpp:10`, cùng namespace
`deskhub::diag`.

### 10. Helper vụn

- [ ] `PathText` ×3 — `client/linux/gtk/MainWindow.cpp:66`,
      `client/windows/win32/MainFrame.cpp:106`, `client/cli/ShareCommand.cpp:33`
- [ ] `FormatLastConnected` ×2 — chỉ khác `localtime_r` / `localtime_s`,
      hợp với `platform/include/deskhubp/system/Clock.h`
      (`client/linux/gtk/MainWindow.cpp:289`, `client/windows/win32/MainFrame.cpp:180`)
- [ ] `HostKeyOf` / `SameHost` / `AddressesOf` ×2 — cùng hai file trên

### 11. Lambda `onApprovalNeeded` giống hệt ở 4 file AgentLoop

- [ ] Cho `AgentLoop::Start` tự set mặc định, xoá cả 4 bản sao

Chỉ gọi `PushPairingRequest`: `client/linux/cpp/AgentLoop.cpp:89`,
`client/windows/cpp/AgentLoop.cpp:110`, `client/macos/app/cpp/AgentLoop.cpp:91`,
`client/android/app/src/main/cpp/AgentLoop.cpp:114`.

### 12. Android nội bộ — plumbing IME trùng ~40 dòng

- [ ] Tách base class dùng chung trong `client/android`

`client/android/app/src/main/java/com/deskhub/app/KeyInputView.kt:24` và `TermInputView`
trong `TerminalActivity.kt:114`.

---

## Đã kiểm tra và **không** phải trùng

Ghi lại để khỏi rà lại lần sau:

- Cấu trúc `HostEnginePolicy` ở 5 file `AgentLoop.cpp` — khác nhau thật ở tầng capture/encode
- `InputInjector` — logic chung đã nằm ở `deskhub::InputApplier` + `deskhubp::LocalInputGate`;
  phần còn lại chỉ là khai báo CRTP với `Init()` khác chữ ký từng OS
- Tầng FFI — Android đã dùng chung `deskhubp/ffi/*`, phần JNI marshalling là Android-specific thật
- File transfer — đã gọn trong `deskhubp::FileTransferClient` + `deskhub::ui::TransferView`
- Contract encoder / decoder / capture — đã ở `core/include/deskhub/media/*Contract.h`
- SwiftUI views iOS vs macOS — UI khác nhau thật (touch single-select vs list multi-select),
  không phải copy
- Viewer — `client/cli/ViewerX11.cpp` và `client/linux/gtk/ViewerWindow.*` đều đã dùng chung
  `deskhubp::ClientEngine<AvDecoder, VideoSink*>`
