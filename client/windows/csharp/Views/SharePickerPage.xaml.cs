using System;
using System.Collections.ObjectModel;
using Deskhub.Interop;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Deskhub.Views;

// SharePickerPage — liệt kê cửa sổ chia sẻ được (THẬT, từ native) và chọn nguồn. Mở
// phiên host thật là M2 (refactor AgentLoop headless + C API dh_agent_start).
public sealed partial class SharePickerPage : Page
{
    private readonly ObservableCollection<CaptureWindow> _windows = new();

    public SharePickerPage()
    {
        InitializeComponent();
        WindowList.ItemsSource = _windows;
        Loaded += (_, _) => Refresh();
    }

    private void Refresh()
    {
        _windows.Clear();
        try
        {
            foreach (var w in NativeMethods.ListWindows())
                _windows.Add(w);
            StatusText.Text = _windows.Count == 0
                ? "No shareable window found."
                : $"{_windows.Count} window(s) found. Select one to share.";
        }
        catch (DllNotFoundException)
        {
            StatusText.Text = "deskhub_native.dll not found — build the cpp/ target first (see README).";
        }
        catch (Exception ex)
        {
            StatusText.Text = "Failed to list windows: " + ex.Message;
        }
    }

    private void OnRefreshClick(object sender, RoutedEventArgs e) => Refresh();

    private void OnSelectionChanged(object sender, SelectionChangedEventArgs e)
        => StartButton.IsEnabled = WindowList.SelectedItem is CaptureWindow;

    private void OnStartClick(object sender, RoutedEventArgs e)
    {
        if (WindowList.SelectedItem is not CaptureWindow w) return;

        ushort port = ushort.TryParse(PortBox.Text.Trim(), out var p) ? p : (ushort)47777;
        uint fps = uint.TryParse(FpsBox.SelectedItem as string, out var f) ? f : 60;
        uint bitrate = ParseLeadingUint(BitrateBox.SelectedItem as string, 20);
        bool allow = AllowControl.IsChecked == true;
        var req = new ShareRequest(w.Hwnd, w.DisplayTitle, port, fps, bitrate, allow);

        // Bật điều khiển / thiếu rule firewall + chưa admin → bung UAC, chạy lại elevated.
        // Người dùng đồng ý: instance mới tiếp quản, đóng instance này. Huỷ UAC: chạy
        // tiếp quyền thường (input tới app admin có thể không ăn — xem ElevatedShare).
        if (Deskhub.ElevationHelper.NeedsElevation(allow)
            && Deskhub.ElevationHelper.TryRelaunchElevated(req))
        {
            Application.Current.Exit();
            return;
        }

        Frame.Navigate(typeof(SharingStatusPage), req);
    }

    // "20 Mbps" -> 20. Trả `fallback` nếu không đọc được số đầu chuỗi.
    private static uint ParseLeadingUint(string? s, uint fallback)
    {
        if (string.IsNullOrEmpty(s)) return fallback;
        int i = 0;
        while (i < s.Length && char.IsDigit(s[i])) i++;
        return i > 0 && uint.TryParse(s[..i], out var v) ? v : fallback;
    }

    private void OnBackClick(object sender, RoutedEventArgs e)
    {
        if (Frame.CanGoBack) Frame.GoBack();
        else Frame.Navigate(typeof(HomePage));
    }
}
