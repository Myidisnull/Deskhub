using System;
using System.Runtime.InteropServices;

namespace Deskhub.Interop;

// =============================================================================
// ViewerSession — bọc handle phiên xem của C API. Giữ delegate sống suốt phiên (GC
// thu là native gọi ngược vào bộ nhớ đã giải phóng → sập). Callback từ native chạy
// trên THREAD NỀN; lớp này phát event thô, phía UI tự marshal về DispatcherQueue.
// =============================================================================
public sealed class ViewerSession : IDisposable
{
    private IntPtr _handle;
    private readonly NativeMethods.DhClientStatsCallback _statsCb;
    private readonly NativeMethods.DhClientSizeCallback _sizeCb;
    private readonly NativeMethods.DhClientClosedCallback _closedCb;

    public event Action<string>? Stats;          // dòng số liệu cho thanh trên
    public event Action<uint, uint>? SizeChanged; // cỡ video đàm phán (đặt tỷ lệ panel)
    public event Action<string>? Closed;          // phiên kết thúc (lý do)

    private ViewerSession()
    {
        _statsCb = OnStats;
        _sizeCb = OnSize;
        _closedCb = OnClosed;
    }

    public static ViewerSession? Start(string addr, byte sourceId, bool sendInput)
    {
        var s = new ViewerSession();
        s._handle = NativeMethods.dh_client_start(addr, sourceId, sendInput ? 1 : 0,
            s._statsCb, s._sizeCb, s._closedCb, IntPtr.Zero);
        return s._handle == IntPtr.Zero ? null : s;
    }

    // Con trỏ IDXGISwapChain để gắn vào SwapChainPanel (ISwapChainPanelNative.SetSwapChain).
    public IntPtr SwapChain =>
        _handle == IntPtr.Zero ? IntPtr.Zero : NativeMethods.dh_client_swapchain(_handle);

    // GĐ9: host có nhận điều khiển không (cờ trong HELLO_ACK). Đáng tin sau khi
    // SizeChanged đầu tiên bắn; trước đó luôn true.
    public bool InputAccepted =>
        _handle == IntPtr.Zero || NativeMethods.dh_client_input_accepted(_handle) != 0;

    public void MouseMove(ushort nx, ushort ny)
    {
        if (_handle != IntPtr.Zero) NativeMethods.dh_client_mouse_move(_handle, nx, ny);
    }

    // Chuột tương đối (chế độ khoá F9): delta thô theo pixel — đường game đọc được.
    public void MouseMoveRel(int dx, int dy)
    {
        if (_handle != IntPtr.Zero) NativeMethods.dh_client_mouse_move_rel(_handle, dx, dy);
    }

    public void MouseButton(int button, bool down)
    {
        if (_handle != IntPtr.Zero) NativeMethods.dh_client_mouse_button(_handle, button, down ? 1 : 0);
    }

    public void Wheel(int delta)
    {
        if (_handle != IntPtr.Zero) NativeMethods.dh_client_wheel(_handle, delta);
    }

    public void Key(int vk, int scan, bool down)
    {
        if (_handle != IntPtr.Zero) NativeMethods.dh_client_key(_handle, vk, scan, down ? 1 : 0);
    }

    private void OnStats(IntPtr s, IntPtr user) => Stats?.Invoke(Marshal.PtrToStringUTF8(s) ?? "");
    private void OnSize(uint w, uint h, IntPtr user) => SizeChanged?.Invoke(w, h);
    private void OnClosed(IntPtr r, IntPtr user) => Closed?.Invoke(Marshal.PtrToStringUTF8(r) ?? "disconnected");

    public void Dispose()
    {
        var h = _handle;
        _handle = IntPtr.Zero;
        if (h != IntPtr.Zero) NativeMethods.dh_client_stop(h); // join thread, thả delegate an toàn
    }
}
