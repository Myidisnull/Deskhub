using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using Deskhub.Interop;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Navigation;
using Windows.System;
using WinRT; // CastExtensions .As<T>() để lấy ISwapChainPanelNative từ SwapChainPanel

namespace Deskhub.Views;

// =============================================================================
// ViewerPage — xem ViewerPage.xaml về bố cục HUD.
//
// F9 KHOÁ CHUỘT, VÀ ĐÓ LÀ CẢ LÝ DO APP NÀY TỒN TẠI
//   Remote desktop thường gửi TOẠ ĐỘ TUYỆT ĐỐI, và game bỏ qua chúng. Khi khoá, ta
//   giấu con trỏ, NEO nó tại chỗ (mỗi lần di là kéo về neo bằng SetCursorPos, thêm
//   ClipCursor quây một ô nhỏ quanh neo phòng cú vẩy nhanh hơn nhịp event), rồi gửi
//   ĐỘ LỆCH thô — đó là thứ game đọc. Con số F9 in ngay trên HUD vì người dùng phải
//   biết cách thoát ra trước khi họ bấm vào.
//
// NHIỀU NGUỒN
//   Tick ≥2 nguồn ở màn chọn thì HUD hiện dãy nút chuyển nhanh. Mỗi cặp (client,
//   nguồn) là một phiên độc lập nên chuyển nguồn = thay phiên — xem SwitchTo.
//
// SỐ LIỆU
//   Native đã dựng sẵn một dòng chữ (fps/Mbps/loss/RTT/e2e) nên HUD in thẳng. Riêng
//   RTT được rút ra bằng regex để nuôi biểu đồ đường — dựng một kênh số riêng qua C
//   API chỉ để lấy một con số đã có trong dòng đó là thêm bề mặt mà không thêm gì.
// =============================================================================
public sealed partial class ViewerPage : Page
{
    private static readonly Regex RttPattern = new(@"RTT\s+(\d+)", RegexOptions.Compiled);

    private ViewerSession? _session;
    private uint _videoW, _videoH;
    private bool _locked;
    private bool _fitToWindow = true;
    private readonly List<double> _trace = new();
    private string _address = "";
    private bool _sendInput = true;               // người dùng chọn ở màn trước
    private bool _inputAccepted = true;           // host chào trong HELLO_ACK (GĐ9)
    private IReadOnlyList<HostSource>? _multi;    // ≥2 nguồn đã tick; null = một nguồn
    private byte _sourceId;
    private Win32Cursor.POINT _lockAnchor;        // vị trí neo con trỏ lúc khoá (toạ độ màn hình)

    public ViewerPage()
    {
        InitializeComponent();
        VideoPanel.IsTabStop = true;
        KeyDown += OnKeyDown;
        KeyUp += OnKeyUp;
    }

    // Đăng ký ở đây, không ở constructor — xem ghi chú ở HomePage.OnNavigatedTo.
    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        AppState.Changed += ApplyLanguage;
        ApplyLanguage();

        string addr = "";
        bool sendInput = true;
        byte sourceId = 0;
        IReadOnlyList<HostSource>? sources = null;
        switch (e.Parameter)
        {
            case ViewerRequest r:
                (addr, sendInput, sourceId, sources) = (r.Address, r.SendInput, r.SourceId, r.Sources);
                break;
            case string s:
                addr = s;
                break;
        }
        _address = addr;
        _sendInput = sendInput;
        _multi = sources is { Count: > 1 } ? sources : null;
        HostLabel.Text = addr;

        if (string.IsNullOrEmpty(addr))
        {
            StatsText.Text = L.T("connectFailed");
            return;
        }

        BuildSourceButtons();
        StartSession(sourceId);
    }

    // Mở phiên xem nguồn `sourceId`. Cũng là đường của nút chuyển nguồn (SwitchTo).
    private void StartSession(byte sourceId)
    {
        _sourceId = sourceId;
        _videoW = _videoH = 0;
        _inputAccepted = true; // chỉ biết thật sau HELLO_ACK — xem OnVideoSize

        _session = ViewerSession.Start(_address, sourceId, _sendInput);
        if (_session is null)
        {
            StatsText.Text = L.T("connectFailed");
            UpdateInputHud();
            return;
        }

        _session.Stats += OnStats;
        _session.SizeChanged += OnVideoSize;
        _session.Closed += OnClosed;

        // Swapchain sẵn sàng ngay sau Start (native tạo đồng bộ trong dh_client_start).
        var sc = _session.SwapChain;
        if (sc != IntPtr.Zero) VideoPanel.As<ISwapChainPanelNative>().SetSwapChain(sc);

        UpdateInputHud();
        HighlightSourceButtons();
        VideoPanel.Focus(FocusState.Programmatic);
    }

    // Phiên chỉ-xem thì giấu nút khoá chuột — vẽ một nút không làm gì là nói dối.
    // "Chỉ xem" có HAI đường tới: người dùng tự chọn (giấu cả chip — họ đã biết),
    // hoặc host không nhận điều khiển (GĐ9 — chip vẫn hiện để NÓI vì sao gõ không ăn).
    private void UpdateInputHud()
    {
        bool canInput = _sendInput && _inputAccepted;
        LockButton.Visibility = canInput ? Visibility.Visible : Visibility.Collapsed;
        if (canInput)
        {
            LockChip.Visibility = Visibility.Visible;
            LockText.Text = L.T(_locked ? "mouseLocked" : "mouseFree");
        }
        else if (_sendInput)
        {
            LockChip.Visibility = Visibility.Visible;
            LockText.Text = L.T("viewOnlySession");
        }
        else
        {
            LockChip.Visibility = Visibility.Collapsed;
        }
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e)
    {
        AppState.Changed -= ApplyLanguage;
        SetLocked(false);
        Stop();
    }

    private void ApplyLanguage()
    {
        EndText.Text = L.T("end");
        UpdateInputHud();
        if (_session is null && string.IsNullOrEmpty(StatsText.Text))
            StatsText.Text = L.T("connecting");
    }

    // --- Nhiều nguồn (tick ≥2 ở màn chọn): dãy nút chuyển nhanh trên HUD ---

    private void BuildSourceButtons()
    {
        SourceButtons.Children.Clear();
        bool multi = _multi is not null;
        SourceButtons.Visibility = multi ? Visibility.Visible : Visibility.Collapsed;
        SourceDivider.Visibility = multi ? Visibility.Visible : Visibility.Collapsed;
        if (!multi) return;

        foreach (var s in _multi!)
        {
            var src = s; // bắt biến theo từng vòng lặp, không phải biến vòng lặp
            var b = new Button
            {
                Content = new TextBlock
                {
                    Text = src.Name,
                    Style = (Style)Application.Current.Resources["MonoText"],
                },
                Style = (Style)Application.Current.Resources["IconButton"],
                Tag = src.SourceId,
                Height = 34,
            };
            b.Click += (_, _) => SwitchTo(src.SourceId);
            SourceButtons.Children.Add(b);
        }
    }

    private void HighlightSourceButtons()
    {
        foreach (var child in SourceButtons.Children)
        {
            if (child is Button b && b.Tag is byte id)
                b.Style = (Style)Application.Current.Resources[
                    id == _sourceId ? "IconButtonActive" : "IconButton"];
        }
    }

    // Chuyển sang xem nguồn khác: đóng phiên hiện tại, mở phiên mới. Mỗi cặp
    // (client, nguồn) là một phiên độc lập nên host không cần biết gì thêm.
    private void SwitchTo(byte sourceId)
    {
        if (sourceId == _sourceId && _session is not null) return;
        SetLocked(false);
        Stop();
        StartSession(sourceId);
    }

    // --- Sự kiện từ native (thread nền) → UI thread ---

    private void OnStats(string line) => DispatcherQueue.TryEnqueue(() =>
    {
        StatsText.Text = line;
        LiveDot.IsLive = true;

        var m = RttPattern.Match(line);
        if (m.Success && int.TryParse(m.Groups[1].Value, NumberStyles.Integer,
                CultureInfo.InvariantCulture, out var rtt))
        {
            _trace.Add(rtt);
            if (_trace.Count > 60) _trace.RemoveAt(0);
            Trace.SetValues(_trace);
        }
    });

    private void OnVideoSize(uint w, uint h) => DispatcherQueue.TryEnqueue(() =>
    {
        _videoW = w;
        _videoH = h;
        HostLabel.Text = $"{_address} — {w}×{h}";
        // Cờ GĐ9 trong HELLO_ACK chỉ đáng tin sau đàm phán — đúng thời điểm sizeCb
        // này bắn. Host không nhận điều khiển thì thôi khoá chuột và nói thẳng trên HUD.
        bool accepted = _session?.InputAccepted ?? true;
        if (accepted != _inputAccepted)
        {
            _inputAccepted = accepted;
            if (!accepted) SetLocked(false);
            UpdateInputHud();
        }
        Relayout();
    });

    private void OnClosed(string reason) => DispatcherQueue.TryEnqueue(() =>
    {
        SetLocked(false);
        Stop();
        if (Frame.CanGoBack) Frame.BackStack.Clear();
        Frame.Navigate(typeof(HomePage));
    });

    // --- Bố cục: giữ tỷ lệ khung video ---

    private void OnContainerSizeChanged(object sender, SizeChangedEventArgs e) => Relayout();

    private void Relayout()
    {
        if (_videoW == 0 || _videoH == 0) return;
        double aw = VideoContainer.ActualWidth, ah = VideoContainer.ActualHeight;
        if (aw <= 0 || ah <= 0) return;

        // Vừa khung: thu nhỏ nếu cần nhưng KHÔNG phóng to quá 1:1 — phóng to một khung
        // 1920 lên màn 4K chỉ cho ra một hình mờ, chứ không thêm chi tiết nào.
        double scale = Math.Min(aw / _videoW, ah / _videoH);
        if (_fitToWindow) scale = Math.Min(scale, 1.0);
        VideoPanel.Width = _videoW * scale;
        VideoPanel.Height = _videoH * scale;
    }

    private void OnToggleScale(object sender, RoutedEventArgs e)
    {
        _fitToWindow = !_fitToWindow;
        Relayout();
    }

    // --- Khoá chuột (F9) ---

    private void OnToggleLock(object sender, RoutedEventArgs e) => SetLocked(!_locked);

    private void SetLocked(bool on)
    {
        if (_locked == on) return;
        if (on && !(_sendInput && _inputAccepted)) return; // phiên chỉ-xem: không có gì để khoá
        _locked = on;
        // Ẩn con trỏ khi khoá: hai con trỏ trên màn (của ta và của máy kia) là thứ
        // khiến người dùng không biết mình đang trỏ vào đâu.
        ProtectedCursor = on
            ? InputSystemCursor.Create(InputSystemCursorShape.UniversalNo)
            : null;
        if (on)
        {
            // Neo con trỏ tại chỗ: mỗi PointerMoved sau đây đo delta so với neo rồi
            // kéo con trỏ VỀ neo (xem OnPointerMoved) nên nó không rời khung được.
            // ClipCursor quây thêm một ô nhỏ quanh neo phòng cú vẩy nhanh hơn nhịp
            // event — không có nó, con trỏ thoát ra ngoài là panel ngừng nhận move.
            Win32Cursor.GetCursorPos(out _lockAnchor);
            var clip = new Win32Cursor.RECT
            {
                Left = _lockAnchor.X - 64,
                Top = _lockAnchor.Y - 64,
                Right = _lockAnchor.X + 64,
                Bottom = _lockAnchor.Y + 64,
            };
            Win32Cursor.ClipCursor(ref clip);
            VideoPanel.Focus(FocusState.Programmatic);
        }
        else
        {
            Win32Cursor.ClipCursor(IntPtr.Zero);
        }
        LockText.Text = L.T(on ? "mouseLocked" : "mouseFree");
        LockButton.Style = (Style)Application.Current.Resources[
            on ? "IconButtonActive" : "IconButton"];
    }

    // --- Chuột ---

    private void OnPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        if (_session is null || !_inputAccepted) return;
        if (_locked)
        {
            // Chế độ khoá: gửi ĐỘ LỆCH thô (thứ game đọc — host bơm MOUSEEVENTF_MOVE)
            // rồi kéo con trỏ về neo để lần sau lại đo từ đó.
            Win32Cursor.GetCursorPos(out var p);
            int dx = p.X - _lockAnchor.X, dy = p.Y - _lockAnchor.Y;
            if (dx != 0 || dy != 0)
            {
                _session.MouseMoveRel(dx, dy);
                Win32Cursor.SetCursorPos(_lockAnchor.X, _lockAnchor.Y);
            }
            return;
        }
        var pt = e.GetCurrentPoint(VideoPanel).Position;
        double w = VideoPanel.ActualWidth, h = VideoPanel.ActualHeight;
        if (w <= 0 || h <= 0) return;
        ushort nx = (ushort)Math.Clamp(pt.X / w * 65535.0, 0, 65535);
        ushort ny = (ushort)Math.Clamp(pt.Y / h * 65535.0, 0, 65535);
        _session.MouseMove(nx, ny);
    }

    private void OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        VideoPanel.Focus(FocusState.Programmatic);
        VideoPanel.CapturePointer(e.Pointer);
        HandleButton(e, down: true);
    }

    private void OnPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        HandleButton(e, down: false);
        VideoPanel.ReleasePointerCapture(e.Pointer);
    }

    private void HandleButton(PointerRoutedEventArgs e, bool down)
    {
        if (!_inputAccepted) return;
        var kind = e.GetCurrentPoint(VideoPanel).Properties.PointerUpdateKind;
        int button = kind switch
        {
            PointerUpdateKind.LeftButtonPressed or PointerUpdateKind.LeftButtonReleased => 0,
            PointerUpdateKind.RightButtonPressed or PointerUpdateKind.RightButtonReleased => 1,
            PointerUpdateKind.MiddleButtonPressed or PointerUpdateKind.MiddleButtonReleased => 2,
            _ => -1,
        };
        if (button >= 0) _session?.MouseButton(button, down);
    }

    private void OnPointerWheel(object sender, PointerRoutedEventArgs e)
    {
        if (!_inputAccepted) return;
        int delta = e.GetCurrentPoint(VideoPanel).Properties.MouseWheelDelta;
        if (delta != 0) _session?.Wheel(delta);
    }

    // --- Bàn phím ---

    private void OnKeyDown(object sender, KeyRoutedEventArgs e)
    {
        // F9 là của UI này, không gửi đi: nó là lối thoát duy nhất khỏi chế độ khoá.
        if (e.Key == VirtualKey.F9)
        {
            SetLocked(!_locked);
            e.Handled = true;
            return;
        }
        SendKey(e, down: true);
    }

    private void OnKeyUp(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == VirtualKey.F9)
        {
            e.Handled = true;
            return;
        }
        SendKey(e, down: false);
    }

    private void SendKey(KeyRoutedEventArgs e, bool down)
    {
        if (_session is null || !_inputAccepted) return;
        int scan = (int)e.KeyStatus.ScanCode | (e.KeyStatus.IsExtendedKey ? 0x100 : 0);
        _session.Key((int)e.Key, scan, down);
        e.Handled = true; // nuốt phím: đang gõ vào máy từ xa, không phải vào UI này
    }

    private void OnDisconnect(object sender, RoutedEventArgs e)
    {
        SetLocked(false);
        Stop();
        if (Frame.CanGoBack) Frame.BackStack.Clear();
        Frame.Navigate(typeof(HomePage));
    }

    private void Stop()
    {
        if (_session is null) return;
        _session.Stats -= OnStats;
        _session.SizeChanged -= OnVideoSize;
        _session.Closed -= OnClosed;
        _session.Dispose();
        _session = null;
    }

    // Neo/quây con trỏ cho chế độ khoá — WinUI3 không có API tương đương, phải xuống
    // thẳng user32. Toạ độ đều là pixel vật lý trên màn hình ảo.
    private static class Win32Cursor
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct POINT
        {
            public int X, Y;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct RECT
        {
            public int Left, Top, Right, Bottom;
        }

        [DllImport("user32.dll")]
        public static extern bool GetCursorPos(out POINT p);

        [DllImport("user32.dll")]
        public static extern bool SetCursorPos(int x, int y);

        [DllImport("user32.dll")]
        public static extern bool ClipCursor(ref RECT r);

        // Bản IntPtr.Zero = bỏ quây (ClipCursor(NULL)).
        [DllImport("user32.dll")]
        public static extern bool ClipCursor(IntPtr r);
    }
}
