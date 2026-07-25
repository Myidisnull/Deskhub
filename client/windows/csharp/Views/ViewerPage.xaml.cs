using System;
using System.Collections.Generic;
using System.Globalization;
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
//   giấu con trỏ và kẹp nó về giữa khung sau mỗi lần di, rồi gửi ĐỘ LỆCH — đó là thứ
//   game đọc. Con số F9 in ngay trên HUD vì người dùng phải biết cách thoát ra trước
//   khi họ bấm vào.
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

        var (addr, sendInput, sourceId) = e.Parameter switch
        {
            ViewerRequest r => (r.Address, r.SendInput, r.SourceId),
            string s => (s, true, (byte)0),
            _ => ("", true, (byte)0),
        };
        _address = addr;
        HostLabel.Text = addr;

        if (string.IsNullOrEmpty(addr))
        {
            StatsText.Text = L.T("connectFailed");
            return;
        }

        _session = ViewerSession.Start(addr, sourceId, sendInput);
        if (_session is null)
        {
            StatsText.Text = L.T("connectFailed");
            return;
        }

        _session.Stats += OnStats;
        _session.SizeChanged += OnVideoSize;
        _session.Closed += OnClosed;

        // Swapchain sẵn sàng ngay sau Start (native tạo đồng bộ trong dh_client_start).
        var sc = _session.SwapChain;
        if (sc != IntPtr.Zero) VideoPanel.As<ISwapChainPanelNative>().SetSwapChain(sc);

        // Phiên chỉ-xem: giấu nút khoá chuột. Vẽ một nút không làm gì là nói dối.
        LockButton.Visibility = sendInput ? Visibility.Visible : Visibility.Collapsed;
        LockChip.Visibility = sendInput ? Visibility.Visible : Visibility.Collapsed;

        VideoPanel.Focus(FocusState.Programmatic);
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
        LockText.Text = L.T(_locked ? "mouseLocked" : "mouseFree");
        if (_session is null && string.IsNullOrEmpty(StatsText.Text))
            StatsText.Text = L.T("connecting");
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
        _locked = on;
        // Ẩn con trỏ khi khoá: hai con trỏ trên màn (của ta và của máy kia) là thứ
        // khiến người dùng không biết mình đang trỏ vào đâu.
        ProtectedCursor = on
            ? InputSystemCursor.Create(InputSystemCursorShape.UniversalNo)
            : null;
        if (on) VideoPanel.Focus(FocusState.Programmatic);
        LockText.Text = L.T(on ? "mouseLocked" : "mouseFree");
        LockButton.Style = (Style)Application.Current.Resources[
            on ? "IconButtonActive" : "IconButton"];
    }

    // --- Chuột ---

    private void OnPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        var pt = e.GetCurrentPoint(VideoPanel).Position;
        double w = VideoPanel.ActualWidth, h = VideoPanel.ActualHeight;
        if (w <= 0 || h <= 0) return;
        ushort nx = (ushort)Math.Clamp(pt.X / w * 65535.0, 0, 65535);
        ushort ny = (ushort)Math.Clamp(pt.Y / h * 65535.0, 0, 65535);
        _session?.MouseMove(nx, ny);
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
        if (_session is null) return;
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
        _session?.Dispose();
        _session = null;
    }
}
