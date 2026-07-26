using System;
using System.Collections.Generic;
using System.Linq;
using Deskhub.Controls;
using Deskhub.Interop;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Windows.ApplicationModel.DataTransfer;

namespace Deskhub.Views;

// Một mục trong danh sách nguồn: cửa sổ hoặc màn hình, kèm việc nó đã được tick chưa
// và (khi đang chia sẻ) sourceId mà native cấp cho nó.
internal sealed class SourceItem
{
    public ulong Hwnd;
    public ulong Monitor;
    public string Name = "";
    public string Resolution = "";
    public bool IsDisplay;
    public bool Selected;

    // Chỉ có nghĩa khi đang chia sẻ và nguồn này đã vào phiên.
    public byte? SourceId;
    public bool Pending;

    // Khoá đồng nhất giữa hai lần liệt kê: HWND/HMONITOR là handle của hệ điều hành,
    // ổn định suốt đời cửa sổ. Dùng tên thì đổi tiêu đề tab trình duyệt là mất tick.
    public ulong Key => IsDisplay ? Monitor : Hwnd;

    public ShareSource ToShareSource()
        => IsDisplay ? ShareSource.Display(Monitor, Name) : ShareSource.Window(Hwnd, Name);
}

// =============================================================================
// SharePage — xem SharePage.xaml về bố cục và vì sao gộp hai màn cũ.
//
// HAI TRẠNG THÁI, MỘT MÀN
//   Chưa chia sẻ: tick nguồn, đặt tham số, bấm "Chia sẻ".
//   Đang chia sẻ: tick/bỏ tick tác động NGAY vào phiên đang chạy; tham số khoá lại
//                 (đổi cổng giữa phiên nghĩa là dựng lại socket, tức ngắt người xem);
//                 hiện thêm "Dừng hết" và panel máy đang xem.
//
// NÂNG QUYỀN
//   Bật điều khiển hoặc chưa có rule firewall thì phải chạy lại elevated. Việc đó
//   thay cả tiến trình, nên nó phải xảy ra TRƯỚC khi mở phiên — xem ElevationHelper.
// =============================================================================
public sealed partial class SharePage : Page
{
    private readonly List<SourceItem> _sources = new();
    private readonly List<double> _trace = new();
    private AgentSession? _session;
    private ushort _port = 47777;
    private bool _sharing;

    public SharePage()
    {
        InitializeComponent();
    }

    // Đăng ký ở đây, không ở constructor — xem ghi chú ở HomePage.OnNavigatedTo.
    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        AppState.Changed += ApplyLanguage;
        ApplyLanguage();
        RefreshSources();
        RebuildAddresses();

        // Bàn giao từ instance vừa được UAC nâng quyền: vào thẳng trạng thái đang chia sẻ.
        if (e.Parameter is ShareRequest req)
        {
            ApplyRequestToUi(req);
            StartSession(req);
        }
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e)
    {
        AppState.Changed -= ApplyLanguage;
        StopSession();
    }

    // --- Chữ ---

    private void ApplyLanguage()
    {
        Eyebrow.Text = L.T("hostMode").ToUpperInvariant();
        Title.Text = L.T("shareTitle");
        Aside.Text = L.T("privateAside");
        AddrLabel.Text = L.T("enterOnOther").ToUpperInvariant();
        ViewerLabel.Text = L.T("connectedViewer").ToUpperInvariant();
        NoViewerTitle.Text = L.T("noViewer");
        NoViewerHint.Text = L.T("noViewerHint");
        DisconnectText.Text = L.T("disconnect");
        StatFps.Label = L.T("sending");
        StatFps.Unit = L.T("fps");
        StatBitrate.Label = L.T("bitrate");
        StatRtt.Label = L.T("roundTrip");
        SourcesLabel.Text = L.T("windowsAndDisplays").ToUpperInvariant();
        RefreshText.Text = L.T("refresh");
        SourcesEmpty.Text = L.T("noSources");
        PortLabel.Text = L.T("port").ToLowerInvariant();
        AllowInput.Content = L.T("allowInput");
        ShareClipboard.Content = L.T("shareClipboard");
        StopText.Text = L.T("stop");
        StopAllText.Text = L.T("stopAll");
        ShareText.Text = L.T("share");
        RebuildSourceList();
    }

    // --- Nguồn ---

    private void RefreshSources()
    {
        // Giữ lại các tick người dùng đã đặt: liệt kê lại là chuyện thường (cửa sổ mở
        // ra đóng vào liên tục), mà mất tick giữa chừng thì không ai tick lại lần hai.
        var chosen = _sources.Where(s => s.Selected).Select(s => s.Key).ToHashSet();
        var live = _sources.Where(s => s.SourceId.HasValue)
            .ToDictionary(s => s.Key, s => s);

        _sources.Clear();
        try
        {
            foreach (var d in NativeMethods.ListDisplays())
            {
                _sources.Add(new SourceItem
                {
                    Monitor = d.Monitor, Name = d.Name, Resolution = d.Resolution,
                    IsDisplay = true, Selected = chosen.Contains(d.Monitor),
                });
            }
            foreach (var w in NativeMethods.ListWindows())
            {
                if (w.Minimized) continue; // thu nhỏ thì WGC không có gì để bắt
                _sources.Add(new SourceItem
                {
                    Hwnd = w.Hwnd, Name = w.DisplayTitle, Resolution = w.Resolution,
                    Selected = chosen.Contains(w.Hwnd),
                });
            }
        }
        catch (DllNotFoundException)
        {
            SourcesEmpty.Text = L.T("dllMissing");
        }

        foreach (var s in _sources)
        {
            if (!live.TryGetValue(s.Key, out var old)) continue;
            s.SourceId = old.SourceId;
            s.Pending = old.Pending;
            s.Selected = true;
        }
        RebuildSourceList();
    }

    private void RebuildSourceList()
    {
        SourceList.Items.Clear();
        foreach (var s in _sources)
        {
            var item = s; // bắt biến theo từng vòng lặp, không phải biến vòng lặp
            bool inSession = item.SourceId.HasValue;
            bool streaming = inSession && !item.Pending;
            SourceList.Items.Add(SourceRow.Build(
                name: item.Name,
                detail: item.Resolution,
                kindLabel: L.T(item.IsDisplay ? "display" : "window"),
                isDisplay: item.IsDisplay,
                selected: item.Selected,
                state: inSession ? (item.Pending ? L.T("starting") : L.T("streaming")) : L.T("idle"),
                // "Đang khởi động" là trung tính chứ không phải mint: mint ở hệ này nghĩa
                // là ĐANG SỐNG, mà nguồn còn pending thì chưa có khung hình nào đi ra.
                tone: streaming ? StatePill.Tone.Live : StatePill.Tone.Neutral,
                canStop: inSession,
                stopLabel: L.T("stop"),
                onToggle: on => ToggleSource(item, on),
                onStop: () =>
                {
                    ToggleSource(item, false);
                    RebuildSourceList(); // ô tick phải bỏ dấu theo, nó không tự biết
                }));
        }
        SourcesEmpty.Visibility = _sources.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        UpdateSharedCount();
    }

    private void UpdateSharedCount()
        => SharedCount.Text = string.Format(L.T("sharedCountFmt"),
            _sources.Count(s => s.Selected), _sources.Count);

    // Tick/bỏ tick. Khi ĐANG chia sẻ thì tác động ngay vào phiên đang chạy — người
    // xem không rớt kết nối, đó là cả lý do màn này gộp làm một.
    private void ToggleSource(SourceItem item, bool on)
    {
        item.Selected = on;
        if (_sharing && _session is not null)
        {
            if (on)
            {
                // Màn hình chỉ thêm được lúc bắt đầu phiên: C API hiện chỉ có
                // dh_agent_add_window. Bỏ tick rồi tick lại một màn hình giữa phiên
                // sẽ không có tác dụng — nói thẳng bằng cách bỏ tick lại.
                if (item.IsDisplay)
                {
                    item.Selected = false;
                    RebuildSourceList();
                    return;
                }
                _session.AddWindow(item.Hwnd, item.Name);
                item.Pending = true;
            }
            else if (item.SourceId is byte id)
            {
                _session.Remove(id);
                item.SourceId = null;
                item.Pending = false;
            }
        }
        UpdateSharedCount();
    }

    // --- Địa chỉ ---

    private void RebuildAddresses()
    {
        AddressList.Children.Clear();
        try
        {
            foreach (var ip in NativeMethods.ListLocalIps())
                AddressList.Children.Add(AddressRow.Build($"{ip.Ip}:{_port}", ip.AdapterName, Copy));
        }
        catch (DllNotFoundException)
        {
            AddressList.Children.Add(new TextBlock
            {
                Text = L.T("dllMissing"),
                Style = (Style)Application.Current.Resources["MonoText"],
            });
        }
    }

    private static void Copy(string text)
    {
        var dp = new DataPackage();
        dp.SetText(text);
        Clipboard.SetContent(dp);
    }

    // --- Phiên ---

    private void OnRefresh(object sender, RoutedEventArgs e) => RefreshSources();

    private void OnShare(object sender, RoutedEventArgs e)
    {
        if (_sharing) return;
        var picked = _sources.Where(s => s.Selected).ToList();
        if (picked.Count == 0) return;

        var req = new ShareRequest(
            picked.Select(s => s.ToShareSource()).ToList(),
            Port: ParsePort(),
            Fps: ParseLeadingUint(FpsBox.SelectedItem as string, 60),
            BitrateMbps: ParseLeadingUint(BitrateBox.SelectedItem as string, 20),
            AllowControl: AllowInput.IsChecked == true,
            ShareClipboard: ShareClipboard.IsChecked == true);

        // Nâng quyền thay cả tiến trình → phải xảy ra TRƯỚC khi mở phiên.
        if (ElevationHelper.NeedsElevation(req.AllowControl)
            && ElevationHelper.TryRelaunchElevated(req))
        {
            Application.Current.Exit();
            return;
        }
        StartSession(req);
    }

    private void StartSession(ShareRequest req)
    {
        _port = req.Port;
        _session = AgentSession.Start(req);
        if (_session is null)
        {
            SourcesEmpty.Text = L.T("shareFailed");
            SourcesEmpty.Visibility = Visibility.Visible;
            return;
        }
        _session.Bound += OnBound;
        _session.RowsChanged += OnRows;
        _session.Stopped += OnAgentStopped;

        _sharing = true;
        SetSharingUi(true);
        RebuildAddresses();
    }

    // "Dừng" giữ tick lại: dừng rồi chia sẻ tiếp là việc hay xảy ra (đứng dậy khỏi máy
    // một lát), và chọn lại năm cửa sổ vì đã bấm nhầm nút là một cái giá quá đắt.
    private void OnStop(object sender, RoutedEventArgs e) => Stop(clearPicks: false);

    // "Dừng hết" bỏ luôn tick: đây là nút cho lúc muốn chắc chắn KHÔNG còn gì đang được
    // chia sẻ nữa — nên nó phải xoá cả ý định, không chỉ phiên.
    private void OnStopAll(object sender, RoutedEventArgs e) => Stop(clearPicks: true);

    private void Stop(bool clearPicks)
    {
        StopSession();
        _sharing = false;
        foreach (var s in _sources)
        {
            s.SourceId = null;
            s.Pending = false;
            if (clearPicks) s.Selected = false;
        }
        SetSharingUi(false);
        ShowViewer(null);
        RebuildSourceList();
    }

    private void StopSession()
    {
        if (_session is null) return;
        _session.Bound -= OnBound;
        _session.RowsChanged -= OnRows;
        _session.Stopped -= OnAgentStopped;
        _session.Dispose();
        _session = null;
    }

    // Tham số phiên khoá lại khi đang chạy: đổi cổng giữa chừng nghĩa là dựng lại
    // socket, tức ngắt đúng người đang xem.
    private void SetSharingUi(bool sharing)
    {
        FpsBox.IsEnabled = !sharing;
        BitrateBox.IsEnabled = !sharing;
        PortBox.IsEnabled = !sharing;
        AllowInput.IsEnabled = !sharing;
        ShareClipboard.IsEnabled = !sharing;
        ShareButton.Visibility = sharing ? Visibility.Collapsed : Visibility.Visible;
        StopButton.Visibility = sharing ? Visibility.Visible : Visibility.Collapsed;
        StopAllButton.Visibility = sharing ? Visibility.Visible : Visibility.Collapsed;
    }

    // --- Sự kiện từ native (thread nền) → UI thread ---

    private void OnBound(ushort port) => DispatcherQueue.TryEnqueue(() =>
    {
        _port = port;
        RebuildAddresses();
    });

    // Phiên TỰ chết (không mở được cổng/GPU, không nguồn nào dùng được, lỗi socket).
    // Không có đường này thì giao diện đứng ở trạng thái "đang chia sẻ" ma vĩnh viễn.
    // Dispose diễn ra ở đây (UI thread) chứ không trong callback native — xem
    // AgentSession.Stopped.
    private void OnAgentStopped(string reason) => DispatcherQueue.TryEnqueue(() =>
    {
        if (!_sharing) return;
        Stop(clearPicks: false);
        SourcesEmpty.Text = string.Format(L.T("shareStoppedFmt"), reason);
        SourcesEmpty.Visibility = Visibility.Visible;
    });

    private void OnRows(IReadOnlyList<AgentRow> rows) => DispatcherQueue.TryEnqueue(() =>
    {
        // Gắn sourceId native cấp vào đúng mục trong danh sách, khớp theo HANDLE
        // (HWND/HMONITOR — nay đi qua được C API). Khớp theo tên như trước vừa sập
        // khi hai cửa sổ trùng tiêu đề (ToDictionary ném ArgumentException ngay trên
        // UI thread) vừa có thể Stop nhầm nguồn.
        var byKey = new Dictionary<ulong, AgentRow>();
        foreach (var r in rows) byKey[r.Key] = r;
        foreach (var s in _sources)
        {
            if (byKey.TryGetValue(s.Key, out var r))
            {
                s.SourceId = r.SourceId;
                s.Pending = r.Pending;
                s.Selected = true;
            }
            else if (s.SourceId.HasValue)
            {
                // Native đã bỏ nguồn này (cửa sổ bị đóng, hoặc không có frame nào).
                s.SourceId = null;
                s.Pending = false;
                s.Selected = false;
            }
        }
        RebuildSourceList();
        ShowViewer(rows.FirstOrDefault(r => r.ViewerConnected));
    });

    private void ShowViewer(AgentRow? row)
    {
        bool has = row is { ViewerConnected: true };
        ViewerBox.Visibility = has ? Visibility.Visible : Visibility.Collapsed;
        NoViewerBox.Visibility = has ? Visibility.Collapsed : Visibility.Visible;
        if (!has)
        {
            _trace.Clear();
            return;
        }

        var r = row!.Value;
        ViewerName.Text = r.ViewerAddr;
        ViewerMeta.Text = $"{r.Name} · {L.T(AllowInput.IsChecked == true ? "keysAndMouse" : "viewOnly")}";
        StatFps.Value = r.Fps.ToString();
        StatBitrate.Value = (r.Kbps / 1000.0).ToString("0.0");
        // RTT chỉ có sau gói FEEDBACK đầu tiên. Hiện "—" chứ không phải 0: 0 ms là một
        // con số tuyệt vời, còn "chưa biết" thì không nói gì cả — hai thứ khác hẳn nhau.
        StatRtt.Value = r.RttMs > 0 ? r.RttMs.ToString() : "—";

        _trace.Add(r.RttMs > 0 ? r.RttMs : 0);
        if (_trace.Count > 60) _trace.RemoveAt(0);
        Trace.SetValues(_trace);
    }

    // --- Đọc tham số từ ComboBox ---

    private ushort ParsePort()
        => ushort.TryParse(PortBox.SelectedItem as string, out var p) ? p : (ushort)47777;

    // "20 Mbps" -> 20. Trả `fallback` nếu không đọc được số đầu chuỗi.
    private static uint ParseLeadingUint(string? s, uint fallback)
    {
        if (string.IsNullOrEmpty(s)) return fallback;
        int i = 0;
        while (i < s.Length && char.IsDigit(s[i])) i++;
        return i > 0 && uint.TryParse(s[..i], out var v) ? v : fallback;
    }

    private void ApplyRequestToUi(ShareRequest req)
    {
        AllowInput.IsChecked = req.AllowControl;
        ShareClipboard.IsChecked = req.ShareClipboard;
        SelectByPrefix(FpsBox, req.Fps.ToString());
        SelectByPrefix(BitrateBox, req.BitrateMbps.ToString());
        SelectByPrefix(PortBox, req.Port.ToString());
    }

    private static void SelectByPrefix(ComboBox box, string prefix)
    {
        for (int i = 0; i < box.Items.Count; i++)
        {
            if (box.Items[i] is string s && s.StartsWith(prefix, StringComparison.Ordinal))
            {
                box.SelectedIndex = i;
                return;
            }
        }
    }
}
