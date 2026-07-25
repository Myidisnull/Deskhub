using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Deskhub.Interop;

// Một dòng nguồn đang chia sẻ (bản .NET của DhAgentRow).
//
// `Label` là chuỗi ghép sẵn tiếng Anh do native dựng — giữ cho log. Giao diện dùng
// các trường rời: nó hiện được hai ngôn ngữ và vẽ mỗi mẩu ở một chỗ khác nhau.
public readonly record struct AgentRow(
    byte SourceId, string Label, bool Pending,
    string Name, uint Width, uint Height, bool IsDisplay,
    bool ViewerConnected, string ViewerAddr, uint Fps, uint Kbps, uint RttMs)
{
    public string Resolution => Width > 0 && Height > 0 ? $"{Width}×{Height}" : "";
}

// Một nguồn để chia sẻ: ĐÚNG MỘT trong Hwnd/Monitor khác 0.
public readonly record struct ShareSource(ulong Hwnd, ulong Monitor, string Name)
{
    public static ShareSource Window(ulong hwnd, string name) => new(hwnd, 0, name);
    public static ShareSource Display(ulong monitor, string name) => new(0, monitor, name);
    public bool IsDisplay => Monitor != 0;
}

// Yêu cầu bắt đầu chia sẻ. Truyền qua Navigate, và cũng là thứ được đóng gói lại
// thành dòng lệnh khi phải bung UAC (xem ElevationHelper).
public sealed record ShareRequest(
    IReadOnlyList<ShareSource> Sources, ushort Port, uint Fps, uint BitrateMbps,
    bool AllowControl, bool ShareClipboard = false);

// =============================================================================
// AgentSession — bọc handle phiên host của C API. Giữ delegate sống suốt phiên (nếu
// để GC thu là native gọi ngược vào bộ nhớ đã giải phóng → sập). Callback từ C API
// chạy trên THREAD NỀN của native; lớp này chỉ phát event thô, phía UI tự marshal về
// DispatcherQueue.
// =============================================================================
public sealed class AgentSession : IDisposable
{
    private IntPtr _handle;
    // Phải giữ tham chiếu để GC không thu hồi trong lúc native còn gọi ngược.
    private readonly NativeMethods.DhAgentRowsCallback _rowsCb;
    private readonly NativeMethods.DhAgentBoundCallback _boundCb;

    // Danh sách nguồn hiện tại (từ vòng Recv). Chạy trên thread nền.
    public event Action<IReadOnlyList<AgentRow>>? RowsChanged;
    // Cổng UDP thật đã bind (có thể khác cổng yêu cầu). Chạy trên thread nền.
    public event Action<ushort>? Bound;

    private AgentSession()
    {
        _rowsCb = OnRows;
        _boundCb = OnBound;
    }

    public static AgentSession? Start(ShareRequest req)
    {
        if (req.Sources.Count == 0) return null;

        var s = new AgentSession();
        var sources = new NativeMethods.DhAgentSource[req.Sources.Count];
        for (int i = 0; i < req.Sources.Count; i++)
        {
            sources[i] = new NativeMethods.DhAgentSource
            {
                Hwnd = req.Sources[i].Hwnd,
                Monitor = req.Sources[i].Monitor,
                Name = req.Sources[i].Name,
            };
        }
        var opt = new NativeMethods.DhAgentOptions
        {
            Port = req.Port,
            Fps = req.Fps,
            BitrateMbps = req.BitrateMbps,
            AllowInput = req.AllowControl ? 1 : 0,
            ShareClipboard = req.ShareClipboard ? 1 : 0,
        };
        s._handle = NativeMethods.dh_agent_start(sources, sources.Length, in opt,
            s._rowsCb, s._boundCb, IntPtr.Zero);
        return s._handle == IntPtr.Zero ? null : s;
    }

    public void AddWindow(ulong hwnd, string name)
    {
        if (_handle != IntPtr.Zero) NativeMethods.dh_agent_add_window(_handle, hwnd, name);
    }

    public void Remove(byte sourceId)
    {
        if (_handle != IntPtr.Zero) NativeMethods.dh_agent_remove(_handle, sourceId);
    }

    private void OnRows(IntPtr rows, int count, IntPtr user)
    {
        var list = new List<AgentRow>(count);
        int stride = Marshal.SizeOf<NativeMethods.DhAgentRow>();
        for (int i = 0; i < count; i++)
        {
            var r = Marshal.PtrToStructure<NativeMethods.DhAgentRow>(rows + i * stride);
            list.Add(new AgentRow(
                r.SourceId,
                Marshal.PtrToStringUTF8(r.Label) ?? string.Empty,
                r.Pending != 0,
                Marshal.PtrToStringUTF8(r.Name) ?? string.Empty,
                r.Width, r.Height, r.IsDisplay != 0,
                r.ViewerConnected != 0,
                Marshal.PtrToStringUTF8(r.ViewerAddr) ?? string.Empty,
                r.Fps, r.Kbps, r.RttMs));
        }
        RowsChanged?.Invoke(list);
    }

    private void OnBound(ushort port, IntPtr user) => Bound?.Invoke(port);

    public void Dispose()
    {
        // dh_agent_stop đặt cờ dừng, join thread nền (callback không còn chạy sau đó),
        // rồi giải phóng handle. Sau lời gọi này delegate được thả an toàn.
        var h = _handle;
        _handle = IntPtr.Zero;
        if (h != IntPtr.Zero) NativeMethods.dh_agent_stop(h);
    }
}
