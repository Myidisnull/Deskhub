using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Deskhub.Interop;

// Một dòng nguồn đang chia sẻ (bản .NET của DhAgentRow).
public readonly record struct AgentRow(byte SourceId, string Label, bool Pending);

// Yêu cầu bắt đầu share, truyền từ SharePickerPage sang SharingStatusPage qua Navigate.
public sealed record ShareRequest(
    ulong Hwnd, string Name, ushort Port, uint Fps, uint BitrateMbps, bool AllowControl);

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
        var s = new AgentSession();
        var sources = new[]
        {
            new NativeMethods.DhAgentSource { Hwnd = req.Hwnd, Monitor = 0, Name = req.Name },
        };
        var opt = new NativeMethods.DhAgentOptions
        {
            Port = req.Port,
            Fps = req.Fps,
            BitrateMbps = req.BitrateMbps,
            AllowInput = req.AllowControl ? 1 : 0,
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
            var row = Marshal.PtrToStructure<NativeMethods.DhAgentRow>(rows + i * stride);
            list.Add(new AgentRow(row.SourceId,
                Marshal.PtrToStringUTF8(row.Label) ?? string.Empty, row.Pending != 0));
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
