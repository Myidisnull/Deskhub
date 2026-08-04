# TODO — gom code trùng lặp ở client về core/platform

Kết quả audit 2026-08-04. Nguyên tắc chung: media/transport/control đã tách tốt,
trùng lặp dồn ở lớp vỏ UI/orchestration. Mẫu hình lặp lại nhiều nhất: core đã có
logic + test nhưng client mobile viết lại bằng Swift/Kotlin vì chưa expose qua FFI.

## Ưu tiên 1 — xoá divergence thật (làm trước)

### 1.1. Bảng phím native→VK về core/platform — XONG
- [x] `platform/include/deskhubp/input/NativeKeyMap.h` (`NativeKeyToWin`/`WinVkToNative`)
      + impl per-OS trong `platform/src/input/`: Linux (evdev), Mac, Ios (HID),
      Android (AKEYCODE), Win (identity), None; chọn trong `platform/CMakeLists.txt`
- [x] `dh_native_key_to_vk` giờ define một lần trong `ClientFfi.cpp` → mọi OS có;
      xoá impl trong client Linux/macOS; xoá `LinuxKeyMap.{h,cpp}`, `MacKeyMap.{h,cpp}`
- [x] Injector Linux/macOS dùng `deskhubp::WinVkToNative`
- [x] iOS: xoá dict `hidToVk`, dùng `DeskhubClient.mapKey` (wrapper chung mới
      `client/apple/swift/KeyMap.swift`, thay `MacKeyMap.swift`)
- [x] Android: JNI `nativeKeyToVk` + xoá bảng `vkFor` trong `KeyInputView.kt`
- [x] Linux GTK: so lock-toggle bằng VK sau khi map (hết hardcode `GDK_KEY_F9`)

### 1.2. Connect-flow: expose qua FFI + ConnectDriver dùng chung — XONG
- [x] `dh_connect_decision` trong `ClientFfi.h` → `DecideAfterSourceQuery`
- [x] iOS/macOS dùng `DeskhubClient.connectDecision`; Android dùng JNI
      `nativeConnectDecision` (trả -1 = hiện picker)
- [x] `platform/include/deskhubp/session/ConnectDriver.h`: `QueryAsync` (thread +
      UiPost + guard pending) + `ConnectOutcome.hasSources()` + `DefaultViewTargets()`;
      win32 thêm `PostFunction`/`kMsgRunFunction` generic, gtk dùng `RunOnMain` + alive
- [x] Query fail thống nhất: cả hai bên dùng `DefaultViewTargets()` (SourceInfo{}),
      xoá nhánh đặc biệt sources-rỗng trong `Viewer.cpp`

### 1.3. Gộp FFI session shell (3 bản copy) — XONG (một phần)
- [x] `StartFfiClientSession<Session, Surface>` + `StopFfiClientSession` trong
      `ClientSessionShell.h`; Apple/Android shell còn ~10 dòng; Win dùng chung phần
      stop (start của Win giữ riêng vì có GPU init + negotiated-gate)
- [x] KHÔNG bỏ được screen-hint Android: `nativeStart` chạy trước khi surface tồn tại
      (surface gắn sau qua `nativeSetSurface`) và NDK không có API đọc kích thước
      màn hình thuần native → `dh_session_set_screen_hint` giữ lại, đóng vai
      `LocalScreenPixels` phiên bản Android (giá trị lấy từ WindowManager phía Kotlin)

### 1.4. UI strings + chống drift hằng số — XONG
- [x] `ui/Strings.h` thêm `CouldNotConnectTo()` + `kScreenRecordingRequired`; FFI thêm
      `dh_could_not_connect` + `DHStrScreenRecordingRequired = 20` (append cuối enum)
- [x] Swift: `DeskhubClient.couldNotConnect` dùng ở StreamModel/SessionModel (iOS hết
      2 message lệch nhau); macOS AgentModel + ConnectView dùng
      `DHStrScreenRecordingRequired`, ConnectView dùng `DHStrShareButton`
      (log LOGW trong `AgentLoop.cpp` giữ nguyên — là log, không phải UI)
- [x] Android: label = `DHStrClientIpPrompt`, busy hiện `DHStrQueryingSources`,
      validate bằng `dh_parse_address` + hiện `DHStrInvalidAddressHint` (đỏ);
      `nativeCouldNotConnect` thay chuỗi tay trong StreamActivity
- [x] `static_assert` trong `JniBridge.cpp` chốt `DHPhase*`, `DHStr*` (3/12/17/18),
      `MouseButton::Left/Right` — reorder enum phía C++ giờ vỡ build Android ngay
- [x] Kotlin thêm hằng đặt tên `STR_CLIENT_IP_PROMPT/QUERYING_SOURCES/INVALID_ADDRESS_HINT`

### 1.5. Pointer lock: chỉ giữ F9, bỏ hẳn F10/pause — XONG
- [x] Bỏ pause: xoá `OnTogglePauseKey` + `paused` + `acceptsInput` khỏi
      `PointerLockState.h`, viết lại `PointerLockStateTests.cpp`, xoá
      `dh_pointer_toggle_pause` + field `paused`/`pauseChanged` khỏi FFI
- [x] Linux: xoá handling `GDK_KEY_F10` + các check `acceptsInput`
- [x] Hint text trong `ViewerTitle.h` vốn không nhắc pause — không cần sửa
- [x] Thêm `kViewerLockToggleVk` (F9) vào `PointerLockState.h` + FFI
      `dh_is_lock_toggle_vk`; Windows dùng hằng này, macOS map key qua
      `DeskhubClient.mapKey` rồi so VK (bỏ `kMacKeyCodeF9`)
- [x] Windows: title đi qua `PointerLockState::TitleFor()`
- [x] (đã làm trong 1.1) Linux: so lock-toggle bằng VK sau khi map `hardware_keycode`
      thay vì so keyval `GDK_KEY_F9`

## Ưu tiên 2 — parity + dọn theo nền tảng — XONG 2026-08-04

### 2.1. Windows — XONG
- [x] `SelfPath()` dedup → `client/windows/cpp/WinPaths.h::SelfExePath()` (KHÁC kế
      hoạch: không đưa vào platform vì API không thể identical giữa OS — wstring vs
      string, chưa có consumer POSIX; để client-local)
- [x] Fix bug narrow từng ký tự: thêm `ToUtf8` vào `WinText.h`, `DoConnect` dùng
      `ToUtf8` + `deskhub::ui::TrimAscii`
- [x] Codec elevated-share → `core/session/ShareArgs.h`
      (`BuildElevatedShareArgs`/`ParseElevatedShareArgs`) + `ShareArgsTests.cpp`;
      `ElevatedShare.cpp` chỉ còn phần ShellExecute/token
- [x] Win32 boilerplate → `WinControls.h`: `ChildControlFactory` (operator()) +
      `PumpMessagesUntil` — thay 3 bản `mk` + 4 message pump
- [x] ĐÃ CÂN NHẮC VÀ HOÃN: win32 bỏ hop FFI về thẳng `ClientEngine` — blast radius
      lớn (GPU init + negotiated-gate + rewrite Viewer/ViewerInput), lợi ích thấp;
      giữ nguyên, xem lại khi đụng viewer Windows lần tới

### 2.2. Share flow — XONG
- [x] `core/session/ShareFlow.h::ClampShareSources` + `ui::ShareClampWarning()` —
      hai client dùng chung, hết lệch prose
- [x] Windows surface `ListDisplaysError()` khi không có display
- [x] Tooltip → `core/media/ShareStatusText.h::ShareStatusTooltip` + test; Linux dùng.
      Windows CHƯA hiện tooltip (SessionWindow là listbox — cần thêm TOOLTIPS_CLASS
      nếu muốn parity UI thật; formatter đã sẵn)

### 2.3. Apple — XONG (build/run cần verify máy Mac)
- [x] `DeskhubClient.ffiList<Raw,Item>` thay 5 chỗ marshal (~89 dòng → ~30)
- [x] `buffered` internal + `viewerBaseTitle`/`pointerSubtitle` wrapper; macOS
      StreamView dùng
- [x] Dead code đã xoá từ đợt trước; thêm đợt này: xoá `SessionModel.swift` macOS,
      view nhận thẳng `ConnectModel`
- [x] `StreamModel.aspectRatio` (fallback 16:9) — iOS dùng; RemoteView macOS giữ
      fallback bounds (hành vi render riêng, không đổi mù)
- [x] `ClientRoute` chung (`client/apple/swift/ClientRoute.swift`, 4 case:
      connect/sourcePicker/stream/sharing) — macOS `.menu`→`.connect`, iOS bỏ
      `AppScreen`
- [x] `ScrollNotchesFromLines` vào `core/input/PointerMap.h` + FFI
      `dh_scroll_notches_from_lines` + test; RemoteView dùng (bỏ struct
      ScrollAccumulator — carry chỉ 1 biến/1 chỗ mỗi client, không đáng)

### 2.4. Lặt vặt — XONG
- [x] Intent trim đã làm ở đợt dọn code chết (còn 3 mảng id/displayName/sizeLabel —
      giữ 3 mảng vì switcher trong stream cần list, không rebuild qua native nữa)
- [x] Convention picker: cả Android + iOS render `displayName`+`sizeLabel`
      (macOS dùng `pickerLabel`) — đã thống nhất từ đợt trước
- [x] `InputApplier::ReleaseAllHeld()` + hook `ReleaseKey` — Win/Linux dùng, có test
      fake-backend; macOS GIỮ RIÊNG (ReleaseAll của nó cần held-state khi iterate để
      tính CGEventFlags — không Take trước được)
- [x] `ui::TrimAscii` + `ui::ParsePositiveUint` + `media::QualityPresetMaxDim` (+tests)
      — cả hai client desktop dùng; combo Windows bỏ CB_SETITEMDATA

## Dọn code chết — XONG 2026-08-04 (kèm 2 tính năng triển khai từ code "chết")

### Đã xoá
- [x] Chuỗi key-tap/key-chord toàn tuyến: FFI `dh_session_key_tap`/`key_chord` +
      forwarder macro + wrapper Swift (`ClientSession`/`StreamModel`) + wrapper
      Kotlin + JNI shim — hotkey đi đường `dh_session_hotkey` riêng
- [x] Swift `mouseWheel` 2 tầng (Apple dùng notches); GIỮ `dh_session_mouse_wheel`
      cho win32. Kotlin `mouseMoveRel` + JNI; GIỮ `dh_session_mouse_move_rel` cho macOS
- [x] Kotlin `PHASE_CONNECTING`; `dh_viewer_count`; Swift `ViewTransform.maxZoom`,
      `Hotkey.id`+`Identifiable`, `Source.width/height`
- [x] `ConnectDriver::pending()`, `ClientEngine::finished()`, `ClientEngine::ReportRendered()`
- [x] Include thừa: `SourceQuery.h` ×2, `<cstdio>` ViewerWindow,
      `Log.h`/`<string>`/`<memory>` ClientSessionApple.mm (GIỮ `<cstdlib>`
      MainMenuWindow — `_wtoi` cần nó)
- [x] macOS `AgentSourceStatus` → `{id, label}`; Kotlin `Source` → `{id, displayName,
      sizeLabel}`, JNI ctor `(ILjava/lang/String;Ljava/lang/String;)V`, Intent còn 3 mảng

### Đã triển khai (thay vì xoá)
- [x] **Pacer wire vào send path**: `SourcePipelineState.pacer`, rate = `curBitrateBps
      × kPacingRateMultiple (3)`, `Gate + SleepUs` từng datagram trong
      `SendEncodedFrame` — hết micro-burst keyframe. A/B loopback: loss 0% cả hai,
      e2e không tăng (45-74ms có pacer vs 107ms không — nhiễu do desktop tĩnh)
- [x] **Esc thoát pointer-lock cho macOS + Windows** (Linux đã có): FFI mới
      `dh_is_escape_vk`; macOS `RemoteView.keyDown` gọi `dh_pointer_escape`;
      win32 `ViewerInput::OnRawInput` gọi `OnEscape()` — test thật trên Windows:
      F9 khoá → Esc nhả ✓

### Giữ có chủ đích
- `DHStrDisconnected = 15` (tránh renumber enum; client desktop dùng bản C++ trực tiếp)
- `NativeKeyMapWin.cpp` (ClientFfi gọi `NativeKeyToWin` vô điều kiện — cần để link)
- Getter chỉ test dùng (`requiredBps`, `hasReference`, `floorUs`, `none/count`…)
- `ClientSessionWin.cpp` tự chép body start (GPU init + negotiated-gate riêng)

## Ưu tiên 3 — XONG 2026-08-04 (3.1 làm phần drift, hoãn phần model)

- [x] 3.1 (một phần) — 3 điểm drift iOS↔Android đã thống nhất: iOS reset
      `scrollCarry` khi transform (wrapper `transform()` trong TouchInputView);
      Android aspect fallback `16f/9f` thay `0f` (khớp `StreamModel.aspectRatio`);
      overlay "ended" của iOS theo `phase == .ended` thay `!endReason.isEmpty`
      (khớp Android)
- [x] HOÃN CÓ CHỦ ĐÍCH: full `TouchGestureModel` trong core — sau khi soi kỹ, phần
      math (cursor/clamp/normalize/notches/gesture) ĐÃ nằm hết trong core qua FFI;
      phần còn lại là glue recognizer (UIGestureRecognizer vs Compose
      pointerInput) gắn chặt platform, gộp được rất ít mà rủi ro hành vi cao.
      Mở lại nếu policy tap/drag/two-finger còn drift lần nữa
- [x] 3.2 `dh_session_snapshot(DHSession*, DHSessionState*)` — 1 struct
      phase+status+reason+size, hết đọc 5 getter không atomic; Swift
      `ClientSession.snapshot()` + `StreamModel.refresh` dùng; Kotlin
      `NativeClient.Snapshot` + JNI, xoá 5 external getter cũ

## CẦN VERIFY Ở MÁY KHÁC (không build/chạy được trên máy Windows này)

- [ ] **Linux**: build `make build-linux` + chạy thật. Đổi nhiều: MainWindow
      (TrimAscii/ParsePositiveUint/QualityPresetMaxDim/ClampShareSources/
      ConnectDriver), ShareWindow (ShareStatusTooltip), ViewerWindow (evdev map qua
      platform, lock-toggle so VK, bỏ F10), InputInjector (WinVkToNative +
      ReleaseAllHeld/ReleaseKey), platform NativeKeyMapLinux. Test tay: connect,
      picker, F9 lock, Esc nhả, phím + chuột trong viewer, share + tooltip, >8 màn
      hình nếu có
- [x] **macOS build** 2026-08-04: `make build-macos` PASS trên máy Mac (Darwin 25.5,
      Xcode). CÒN LẠI test tay: connect/picker/share, F9 + Esc trong viewer, gõ
      phím, cuộn chuột thường (line-scroll) + trackpad, status share list
- [x] **iOS build** 2026-08-04: `make build-ios` (simulator) PASS. CÒN LẠI test tay:
      connect, stream, touch (tap/double-tap/long-press-drag/2 ngón cuộn + pan khi
      zoom), bàn phím cứng nếu có, switch source
- [x] **swiftlint --strict** + **clang-tidy** 2026-08-04: chạy local (swiftlint
      0.65.0, clang-tidy 22.1.8 pinned như CI) — CI đỏ ở commit 7aa59dc, đã sửa:
      5 lỗi `multiple_closures_with_trailing_closure` (call site `ffiList`),
      clang-format `WinControls.h`, `bugprone-branch-clone` (LOGW/LOGI trong
      `ClientEngine.h` — desktop LOGW = LOGI nên then/else trùng),
      `bugprone-incorrect-roundings` (`ScrollNotchesFromLines`),
      `bugprone-exception-escape` (`~HostEngine` — Stop() có thể throw). Sau sửa:
      `make lint` OK, clang-tidy sạch, `make test` PASS. Chờ push để CI xác nhận
- [x] Coverage core 2026-08-04: `make coverage` + `check-coverage.sh` =
      **99.88% lines / 87.81% branches** (min 90/80) — PASS

## Ghi chú — lỗ hổng chung (chưa client nào có, nếu làm thì làm ở tầng chung)

- Reconnect/backoff: chưa tồn tại ở đâu; hết `kHelloGiveUpUs` 10s là `Dead`, client
  phải tự `dh_session_start` lại
- Settings persistence: chưa có tầng nào lưu lựa chọn user (quality preset, address
  gần nhất…) — nếu cần thì đặt vào core/platform ngay từ đầu
