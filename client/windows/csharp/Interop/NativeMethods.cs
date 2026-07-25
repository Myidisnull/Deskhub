using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Deskhub.Interop;

// =============================================================================
// NativeMethods — cầu P/Invoke sang deskhub_native.dll (C API ở cpp/DeskhubApi.h).
//
// LƯU Ý CHUỖI UTF-8: C API trả char* UTF-8. Marshaling string mặc định của .NET dùng
// codepage ANSI hệ thống, KHÔNG phải UTF-8 — tiêu đề cửa sổ tiếng Việt sẽ hỏng. Nên
// callback nhận IntPtr rồi tự Marshal.PtrToStringUTF8. Mọi chuỗi chỉ sống trong phạm
// vi callback, phải copy ngay (PtrToStringUTF8 đã tạo string mới nên an toàn).
//
// GIỮ DELEGATE SỐNG: GC có thể thu hồi delegate đang được native gọi ngược -> phải
// GC.KeepAlive quanh lời gọi có callback đồng bộ (ở đây native gọi cb NGAY trong hàm,
// nên KeepAlive sau lời gọi là đủ).
// =============================================================================
internal static class NativeMethods
{
    private const string Dll = "deskhub_native.dll";

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate void IpCallback(IntPtr namePtr, IntPtr ipPtr, IntPtr user);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate void WindowCallback(ulong hwnd, IntPtr exePtr, IntPtr titlePtr,
        uint width, uint height, int minimized, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    private static extern int dh_list_local_ips(IpCallback cb, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    private static extern int dh_list_windows(WindowCallback cb, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    private static extern int dh_api_version();

    // --- Bọc thành API .NET thân thiện ------------------------------------------------

    public static int ApiVersion() => dh_api_version();

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    private static extern int dh_is_elevated();

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    private static extern int dh_host_firewall_rule_present();

    public static bool IsElevated() => dh_is_elevated() != 0;
    public static bool FirewallRulePresent() => dh_host_firewall_rule_present() != 0;

    public static List<LocalIp> ListLocalIps()
    {
        var result = new List<LocalIp>();
        IpCallback cb = (namePtr, ipPtr, _) =>
            result.Add(new LocalIp(
                Marshal.PtrToStringUTF8(namePtr) ?? string.Empty,
                Marshal.PtrToStringUTF8(ipPtr) ?? string.Empty));
        dh_list_local_ips(cb, IntPtr.Zero);
        GC.KeepAlive(cb);
        return result;
    }

    public static List<CaptureWindow> ListWindows()
    {
        var result = new List<CaptureWindow>();
        WindowCallback cb = (hwnd, exePtr, titlePtr, w, h, minimized, _) =>
            result.Add(new CaptureWindow(
                hwnd,
                Marshal.PtrToStringUTF8(exePtr) ?? string.Empty,
                Marshal.PtrToStringUTF8(titlePtr) ?? string.Empty,
                w, h, minimized != 0));
        dh_list_windows(cb, IntPtr.Zero);
        GC.KeepAlive(cb);
        return result;
    }

    // --- Vai HOST (M2) — xem cpp/DeskhubApi.h §VAI HOST ----------------------------

    [StructLayout(LayoutKind.Sequential)]
    internal struct DhAgentSource
    {
        public ulong Hwnd;
        public ulong Monitor;
        [MarshalAs(UnmanagedType.LPUTF8Str)] public string? Name;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct DhAgentOptions
    {
        public ushort Port;
        public uint Fps;
        public uint BitrateMbps;
        public int AllowInput;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct DhAgentRow
    {
        public byte SourceId;
        public IntPtr Label; // char* UTF-8, đọc bằng Marshal.PtrToStringUTF8
        public int Pending;
    }

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void DhAgentRowsCallback(IntPtr rows, int count, IntPtr user);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void DhAgentBoundCallback(ushort port, IntPtr user);

    // Mảng struct có trường string LPUTF8Str: marshaler tự chuyển UTF-8 cho từng phần
    // tử. Native copy name vào std::string TRONG lời gọi (đồng bộ) nên chuỗi tạm được
    // giải phóng sau đó là an toàn.
    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern IntPtr dh_agent_start(
        [In] DhAgentSource[] sources, int count, in DhAgentOptions opt,
        DhAgentRowsCallback rowsCb, DhAgentBoundCallback boundCb, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern void dh_agent_add_window(IntPtr h, ulong hwnd,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern void dh_agent_remove(IntPtr h, byte sourceId);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern void dh_agent_stop(IntPtr h);

    // --- Vai CLIENT / VIEWER (M3) — xem cpp/DeskhubApi.h §VAI CLIENT ----------------

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void DhClientStatsCallback(IntPtr statsUtf8, IntPtr user);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void DhClientSizeCallback(uint width, uint height, IntPtr user);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void DhClientClosedCallback(IntPtr reasonUtf8, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern IntPtr dh_client_start(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string addr, byte sourceId, int sendInput,
        DhClientStatsCallback statsCb, DhClientSizeCallback sizeCb,
        DhClientClosedCallback closedCb, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern IntPtr dh_client_swapchain(IntPtr h);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern void dh_client_mouse_move(IntPtr h, ushort nx, ushort ny);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern void dh_client_mouse_button(IntPtr h, int button, int down);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern void dh_client_wheel(IntPtr h, int delta);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern void dh_client_key(IntPtr h, int vk, int scan, int down);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern void dh_client_stop(IntPtr h);
}

// ISwapChainPanelNative — interface COM để gắn IDXGISwapChain của native vào
// SwapChainPanel (WinUI3). Lấy từ panel bằng WinRT CastExtensions .As<T>().
[ComImport]
[Guid("63aad0b8-7c24-40ff-85a8-640d944cc325")]
[InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface ISwapChainPanelNative
{
    [PreserveSig] int SetSwapChain(IntPtr swapChain);
}

// Một địa chỉ IPv4 của máy này (tên adapter + IP), cho màn "Share this PC".
public readonly record struct LocalIp(string AdapterName, string Ip);

// Một cửa sổ chia sẻ được, cho màn SharePicker. Hwnd giữ lại để đưa xuống lúc bắt đầu
// share ở M2.
public readonly record struct CaptureWindow(
    ulong Hwnd, string ExeName, string Title, uint Width, uint Height, bool Minimized)
{
    // Nhãn hiển thị: ưu tiên tiêu đề, kèm độ phân giải như ảnh Mac ("Code — Deskhub").
    public string DisplayTitle => string.IsNullOrWhiteSpace(Title) ? ExeName : Title;
    public string Resolution => $"{Width}×{Height}";
}
