using System;
using Deskhub.Interop;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Navigation;
using WinRT; // CastExtensions .As<T>() để lấy ISwapChainPanelNative từ SwapChainPanel

namespace Deskhub.Views;

// ViewerPage — phiên xem. Bắt đầu ViewerSession, gắn swapchain của native vào
// SwapChainPanel, giữ tỷ lệ khung, và chuyển input xuống C API.
public sealed partial class ViewerPage : Page
{
    private ViewerSession? _session;
    private uint _videoW, _videoH;

    public ViewerPage()
    {
        InitializeComponent();
        // Nhận phím: SwapChainPanel là UIElement nên đặt tab stop rồi lấy focus.
        VideoPanel.IsTabStop = true;
        KeyDown += OnKeyDown;
        KeyUp += OnKeyUp;
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        string addr = e.Parameter as string ?? "";
        if (string.IsNullOrEmpty(addr))
        {
            StatsText.Text = "No address.";
            return;
        }

        _session = ViewerSession.Start(addr, sourceId: 0, sendInput: true);
        if (_session is null)
        {
            StatsText.Text = "Failed to connect (bad address or no device).";
            return;
        }

        _session.Stats += OnStats;
        _session.SizeChanged += OnVideoSize;
        _session.Closed += OnClosed;

        // Gắn swapchain của native vào panel. SwapChain sẵn sàng ngay sau Start (native
        // tạo swapchain đồng bộ trong dh_client_start).
        var sc = _session.SwapChain;
        if (sc != IntPtr.Zero)
        {
            var native = VideoPanel.As<ISwapChainPanelNative>();
            native.SetSwapChain(sc);
        }
        VideoPanel.Focus(FocusState.Programmatic);
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e) => Stop();

    // --- Event từ native (thread nền) → UI thread ---

    private void OnStats(string line) => DispatcherQueue.TryEnqueue(() => StatsText.Text = line);

    private void OnVideoSize(uint w, uint h) => DispatcherQueue.TryEnqueue(() =>
    {
        _videoW = w;
        _videoH = h;
        Relayout();
    });

    private void OnClosed(string reason) => DispatcherQueue.TryEnqueue(() =>
    {
        Stop();
        if (Frame.CanGoBack) Frame.BackStack.Clear();
        Frame.Navigate(typeof(HomePage));
    });

    // --- Bố cục giữ tỷ lệ khung video trong vùng hiển thị ---

    private void OnContainerSizeChanged(object sender, SizeChangedEventArgs e) => Relayout();

    private void Relayout()
    {
        if (_videoW == 0 || _videoH == 0) return;
        double aw = VideoContainer.ActualWidth, ah = VideoContainer.ActualHeight;
        if (aw <= 0 || ah <= 0) return;
        double scale = Math.Min(aw / _videoW, ah / _videoH);
        VideoPanel.Width = _videoW * scale;
        VideoPanel.Height = _videoH * scale;
    }

    // --- Input: chuẩn hoá về 0..65535 trong khung video ---

    private void SendMove(PointerRoutedEventArgs e)
    {
        var pt = e.GetCurrentPoint(VideoPanel).Position;
        double w = VideoPanel.ActualWidth, h = VideoPanel.ActualHeight;
        if (w <= 0 || h <= 0) return;
        ushort nx = (ushort)Math.Clamp(pt.X / w * 65535.0, 0, 65535);
        ushort ny = (ushort)Math.Clamp(pt.Y / h * 65535.0, 0, 65535);
        _session?.MouseMove(nx, ny);
    }

    private void OnPointerMoved(object sender, PointerRoutedEventArgs e) => SendMove(e);

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

    private void OnKeyDown(object sender, KeyRoutedEventArgs e) => SendKey(e, down: true);
    private void OnKeyUp(object sender, KeyRoutedEventArgs e) => SendKey(e, down: false);

    private void SendKey(KeyRoutedEventArgs e, bool down)
    {
        if (_session is null) return;
        int scan = (int)e.KeyStatus.ScanCode | (e.KeyStatus.IsExtendedKey ? 0x100 : 0);
        _session.Key((int)e.Key, scan, down);
        e.Handled = true; // nuốt phím: đang gõ vào máy từ xa, không phải vào UI này
    }

    private void OnDisconnectClick(object sender, RoutedEventArgs e)
    {
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
