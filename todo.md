# TODO — Stop & attach shell trên các client còn lại

Linux đã xong (nút *Stop & attach* trên bảng host, cửa sổ terminal local, docs H-14).
Tầng dùng chung đã sẵn cho mọi client, không cần đụng lại:

- `deskhub::TerminalState::Local` + `TerminalSessions::AttachLocal` (`core/src/session/TerminalSession.cpp`)
- Row model + label: `HostRow::shellState`, `ui::kAttachShellAction`, `ui::kTerminalLocalClient`,
  `ui::kTerminalLocalWindowTitle`, `ui::kTerminalAttachedHere` (`core/.../HostRows.*`, `Strings.h`)
- Snapshot dùng chung: `deskhub::term::TerminalSnapshot` + `SnapshotScreen` (`core/terminal/Snapshot.h`)
- API host: `TerminalHost::AttachLocal / CloseLocal / LocalAlive / LocalSnapshot /
  SendLocalKey / SendLocalText / ResizeLocal` (`platform/src/session/TerminalHost.cpp`)
- Test mẫu end-to-end: `TestHostStopsAndAttachesShell` (`platform/tests/session/TerminalHostTests.cpp`)

## Windows (làm trước — cùng pattern C++ với Linux, rủi ro thấp)

`MainFrame.cpp` dùng thẳng `deskhubp::TerminalHost` nên chỉ là việc UI:

1. `client/windows/win32/TerminalWindow.cpp` — refactor theo đúng mô hình đã làm ở
   `client/linux/gtk/TerminalWindow.cpp`: interface `TerminalFeed` (Alive / Snapshot /
   SendKey / Resize / Shutdown) với hai hiện thực `RemoteFeed` (bọc `TerminalViewer`)
   và `LocalShellFeed` (bọc `TerminalHost&` + `termId`). Renderer wx giữ nguyên,
   chỉ đổi chỗ lấy snapshot và gửi phím. Thêm entry `OpenHostTerminalWindow(...)`.
2. `client/windows/win32/MainFrame.cpp` (~dòng 1559 `BuildHostRows`, ~1428 `KickSession`):
   - thêm nút *Stop & attach* (`ui::kAttachShellAction`) cho dòng shell có
     `row.terminal && row.viewer && row.shellState != TerminalState::Local`;
   - handler: `terminalHost_.AttachLocal(termId)` → refresh bảng → mở cửa sổ local;
     mở fail thì `KickSession(termId)` để không mồ côi shell;
   - dòng shell state `Local`: nút action đổi nhãn thành `ui::kStopDisplayAction`
     (đóng shell), không hiện nút attach nữa (bảng tự rebuild vì `shellState`
     nằm trong `HostRow` equality).
3. Trạng thái cửa sổ local: mở với `ui::kTerminalAttachedHere`; khi `LocalAlive`
   thành false → hiện `ui::kTerminalClosed`, dừng redraw timer. Đóng cửa sổ →
   `CloseLocal` (đóng shell luôn, giống Linux).
4. Kiểm chứng: máy dev không có MSVC — đẩy branch để CI Windows build; lưu ý
   `/W4 /permissive-` gần như warnings-as-errors.

## macOS (khối lượng lớn hơn, không build được từ máy Linux)

Host UI là Swift qua FFI; FFI hiện chỉ có `dha_terminal_active / dha_kick_shell /
dha_stop_terminal` (`platform/include/deskhubp/ffi/AgentSession.h`).

1. Mở rộng FFI trong `platform/src/ffi/AgentSession.cpp` + header, phủ đủ:
   `dha_attach_shell(term_id)`, `dha_local_shell_alive(term_id)`,
   `dha_local_snapshot(...)` (theo kiểu hợp đồng buffer của `TerminalFfi.h` /
   `dh_term_*` để Swift tái dùng cách đọc cell), `dha_local_send_key/text`,
   `dha_local_resize`, `dha_close_local_shell`.
2. `client/macos/app/swift/AgentModel.swift` (~144–150): thêm action attach cho
   dòng shell; `HostPage.swift` (~187–219): nút *Stop & attach* — label lấy từ
   core strings nếu đã có cầu strings, không hardcode.
3. Cửa sổ terminal local: tái dùng renderer của terminal viewer Swift hiện có
   (đường `dh_term_*` / `ClientRoute.swift`) bằng cách tách nguồn snapshot giống
   `TerminalFeed` bên C++ — một nguồn từ `dh_term_*`, một nguồn từ `dha_local_*`.
4. Kiểm chứng: cần máy macOS/Xcode; ít nhất chạy `make lint` (SwiftLint --strict
   trong CI) và CI macOS.

## Không làm

- Android / iOS: không host shell được (`PtyNone.cpp` luôn fail) — không có gì để attach.

## Docs khi làm xong

- `docs/SPECIFICATION.md` H-14 đang ghi "(Linux)" — đổi thành danh sách nền tảng
  thực tế khi Windows/macOS xong, mirror sang `docs/SPECIFICATION.vi.md` cùng commit.
- `docs/ARCHITECTURE.md` §host + "Decisions worth remembering" đã mô tả cơ chế
  mirror chung, không cần sửa thêm trừ khi macOS đổi cách render.
