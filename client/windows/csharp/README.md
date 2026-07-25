# Deskhub — frontend WinUI3 (Windows)

Giao diện Windows viết bằng **WinUI3 / C#**, thay cho lớp UI Win32 cũ (`../cpp/ui`).
Mô phỏng giao diện bản Mac (SwiftUI) — xem ảnh trong `docs/imgs`.

## Kiến trúc hai lớp (giống `client/android`)

```
client/windows/
├── cpp/      → deskhub_native.dll  (C++ pipeline + C API, dựng bằng CMake)
└── csharp/   → Deskhub.exe         (giao diện WinUI3, dựng bằng MSBuild)  ← thư mục này
```

`csharp/` gọi xuống `cpp/` qua P/Invoke (`Interop/NativeMethods.cs` ↔ `cpp/DeskhubApi.h`).

## Yêu cầu

- Visual Studio 2022/18 với workload **.NET Desktop** + **Windows App SDK** (WinUI3),
  hoặc `dotnet` SDK 9 kèm workload tương ứng.
- Đã dựng lớp native trước (tạo ra `deskhub_native.dll`):

```powershell
# từ gốc repo, trong Developer prompt
cmake --preset x64-debug
cmake --build --preset x64-debug --target deskhub_native
```

## Build + chạy

```powershell
cd client/windows/csharp
dotnet build -c Debug        # hoặc mở Deskhub.csproj trong Visual Studio rồi F5
```

`.csproj` tự chép `deskhub_native.dll` từ `out/build/x64-debug/...` sang cạnh
`Deskhub.exe`. Nếu build native ở preset khác, truyền đường dẫn:

```powershell
dotnet build -c Debug -p:DeskhubNativeDll=<đường dẫn tới deskhub_native.dll>
```

## Trạng thái (lộ trình)

| Màn | Nguồn | Trạng thái | Ghi chú |
|-----|-------|-----------|---------|
| Home | ảnh #1 | ✅ M1 | hai card Connect / Share this PC |
| Connect | ảnh #2 | ✅ M3 | mở ViewerPage khi bấm Connect |
| Share picker | ảnh #3 | ✅ M2 | list cửa sổ thật; "Start sharing" mở phiên host |
| Sharing status | ảnh #4 | ✅ M2 | IP + list nguồn + Stop + Add source |
| Viewer + stats | ảnh #5 | ✅ M3 | video vào SwapChainPanel, chuột/phím, thanh số liệu |

**Còn lại — M4**: chuyển UAC elevation (bơm input tới app admin) lên C#, và
`dotnet publish -c Release -r win-x64` self-contained rồi nén thư mục output.

> ⚠️ Phần C# CHƯA build/chạy thử được ở máy dev (không có .NET SDK ở đó). Native
> (`../cpp`) đã build + verify bằng CMake. Hai chỗ cần chú ý khi bạn build bằng VS:
> (1) `SwapChainPanel.As<ISwapChainPanelNative>()` cần CsWinRT (`using WinRT;`) — nếu
> báo lỗi, thay bằng QueryInterface thủ công. (2) Phiên bản NuGet WindowsAppSDK có thể
> cần chỉnh. Gửi tôi log nếu vướng.

Lớp native (`../cpp`) vẫn build ra `client.exe` cũ (Win32) để còn đường lui trong lúc
chuyển. Khi WinUI3 phủ hết vai trò, sẽ gỡ `client.exe` + `cpp/ui`.

## Đóng gói (M4)

App ở chế độ **unpackaged + self-contained** (`WindowsPackageType=None`,
`WindowsAppSDKSelfContained=true`): xuất ra một thư mục chạy được (không cài MSIX,
không cài runtime). `dotnet publish -c Release -r win-x64` rồi nén thư mục output.
