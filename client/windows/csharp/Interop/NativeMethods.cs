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

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate void DisplayCallback(ulong monitor, IntPtr namePtr, uint width,
        uint height, int primary, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    private static extern int dh_list_displays(DisplayCallback cb, IntPtr user);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate void HostFoundCallback(uint hostId, IntPtr namePtr, IntPtr addrPtr,
        uint rttMs, int sourceCount, int acceptsInput, int busy, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    private static extern int dh_discover_scan(ushort port, uint timeoutMs,
        HostFoundCallback cb, IntPtr user);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate void SourceFoundCallback(byte sourceId, IntPtr namePtr,
        int width, int height, int isDisplay, IntPtr user);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    private static extern int dh_client_list_sources(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string addr, SourceFoundCallback cb, IntPtr user);

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

    public static List<CaptureDisplay> ListDisplays()
    {
        var result = new List<CaptureDisplay>();
        DisplayCallback cb = (monitor, namePtr, w, h, primary, _) =>
            result.Add(new CaptureDisplay(monitor,
                Marshal.PtrToStringUTF8(namePtr) ?? string.Empty, w, h, primary != 0));
        dh_list_displays(cb, IntPtr.Zero);
        GC.KeepAlive(cb);
        return result;
    }

    // ⚠️ CHẶN tới ~timeoutMs — gọi trong Task.Run, không bao giờ trên UI thread.
    // Mỗi lần gọi là một lượt quét độc lập; danh sách "đang cập nhật" của giao diện là
    // vòng lặp gọi lại hàm này, không phải một luồng nền phải dọn dẹp.
    public static List<FoundHost> Scan(ushort port, uint timeoutMs)
    {
        var result = new List<FoundHost>();
        HostFoundCallback cb = (hostId, namePtr, addrPtr, rtt, sources, input, busy, _) =>
            result.Add(new FoundHost(hostId,
                Marshal.PtrToStringUTF8(namePtr) ?? string.Empty,
                Marshal.PtrToStringUTF8(addrPtr) ?? string.Empty,
                rtt, sources, input != 0, busy != 0));
        dh_discover_scan(port, timeoutMs, cb, IntPtr.Zero);
        GC.KeepAlive(cb);
        return result;
    }

    // ⚠️ CHẶN tới ~3 giây — gọi trong Task.Run, không bao giờ trên UI thread.
    //
    // DANH SÁCH RỖNG KHÔNG PHẢI LÀ LỖI. Host đời trước GĐ6 không biết LIST_SOURCES và
    // sẽ im lặng suốt 3 giây. Người gọi hiểu là "chỉ có một nguồn" rồi đi thẳng vào
    // nguồn 0 — đúng hành vi trước khi có màn chọn nguồn.
    public static List<HostSource> ListSources(string address)
    {
        var result = new List<HostSource>();
        SourceFoundCallback cb = (id, namePtr, w, h, isDisplay, _) =>
            result.Add(new HostSource(id,
                Marshal.PtrToStringUTF8(namePtr) ?? string.Empty, w, h, isDisplay != 0));
        dh_client_list_sources(address, cb, IntPtr.Zero);
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
        public int ShareClipboard;
    }

    // Layout PHẢI khớp từng trường với DhAgentRow trong cpp/DeskhubApi.h. Sai một
    // trường ở đây không gây lỗi biên dịch — nó đọc lệch bộ nhớ lúc chạy, thường hiện
    // ra thành chuỗi rác hoặc sập trong PtrToStringUTF8.
    [StructLayout(LayoutKind.Sequential)]
    internal struct DhAgentRow
    {
        public byte SourceId;
        public IntPtr Label; // char* UTF-8, đọc bằng Marshal.PtrToStringUTF8
        public int Pending;

        public IntPtr Name;
        public uint Width;
        public uint Height;
        public int IsDisplay;
        public int ViewerConnected;
        public IntPtr ViewerAddr;
        public uint Fps;
        public uint Kbps;
        public uint RttMs;

        // Handle HĐH của nguồn — khoá khớp dòng ↔ SourceItem (tên trùng nhau được).
        public ulong Hwnd;
        public ulong Monitor;
    }

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void DhAgentRowsCallback(IntPtr rows, int count, IntPtr user);

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void DhAgentBoundCallback(ushort port, IntPtr user);

    // Phiên host TỰ kết thúc (không qua dh_agent_stop). Chạy trên thread nền của
    // native — KHÔNG được Dispose đồng bộ trong callback (nó join chính thread đó);
    // marshal về UI thread trước.
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    internal delegate void DhAgentStoppedCallback(IntPtr reasonUtf8, IntPtr user);

    // Mảng struct có trường string LPUTF8Str: marshaler tự chuyển UTF-8 cho từng phần
    // tử. Native copy name vào std::string TRONG lời gọi (đồng bộ) nên chuỗi tạm được
    // giải phóng sau đó là an toàn.
    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern IntPtr dh_agent_start(
        [In] DhAgentSource[] sources, int count, in DhAgentOptions opt,
        DhAgentRowsCallback rowsCb, DhAgentBoundCallback boundCb,
        DhAgentStoppedCallback stoppedCb, IntPtr user);

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
    internal static extern void dh_client_mouse_move_rel(IntPtr h, int dx, int dy);

    [DllImport(Dll, CallingConvention = CallingConvention.StdCall)]
    internal static extern int dh_client_input_accepted(IntPtr h);

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

// Một cửa sổ chia sẻ được, cho danh sách nguồn ở màn chia sẻ.
public readonly record struct CaptureWindow(
    ulong Hwnd, string ExeName, string Title, uint Width, uint Height, bool Minimized)
{
    public string DisplayTitle => string.IsNullOrWhiteSpace(Title) ? ExeName : Title;
    public string Resolution => $"{Width}×{Height}";
}

// Một màn hình chia sẻ được. Màn chia sẻ hiện cả cửa sổ lẫn màn hình trong CÙNG một
// danh sách — hệ quả riêng tư của hai loại khác nhau nên chúng phải phân biệt được
// bằng mắt, nhưng chúng là cùng một loại lựa chọn với người dùng.
public readonly record struct CaptureDisplay(
    ulong Monitor, string Name, uint Width, uint Height, bool Primary)
{
    public string Resolution => $"{Width}×{Height}";
}

// Một nguồn host đang chia sẻ, cho màn "Chọn thứ muốn xem". `SourceId` là thứ đưa
// vào ViewerSession.Start — không phải chỉ số trong danh sách: host cấp id tăng dần
// và KHÔNG tái dùng id của nguồn đã tắt, nên hai thứ đó lệch nhau ngay khi người bên
// kia bỏ chia sẻ một cửa sổ.
public readonly record struct HostSource(
    byte SourceId, string Name, int Width, int Height, bool IsDisplay)
{
    public string Resolution => $"{Width}×{Height}";
}

// Một máy tìm thấy trong mạng (GĐ9). `Busy` = đã có client khác;
// `AcceptsInput` = false nghĩa là kết nối vào sẽ chỉ xem được.
public readonly record struct FoundHost(
    uint HostId, string Name, string Address, uint RttMs, int SourceCount,
    bool AcceptsInput, bool Busy);
