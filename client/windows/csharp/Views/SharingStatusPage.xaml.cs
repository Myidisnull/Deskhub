using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using Deskhub.Interop;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Windows.ApplicationModel.DataTransfer;

namespace Deskhub.Views;

// Một địa chỉ hiển thị cho máy kia gõ vào ("ip:port  (adapter)") + phần copy gọn.
public sealed record AddressEntry(string Display, string CopyText);

// SharingStatusPage — quản lý phiên host đang chạy. Bắt đầu AgentSession trong
// OnNavigatedTo, cập nhật UI từ event (marshal về DispatcherQueue), dừng khi rời trang.
public sealed partial class SharingStatusPage : Page
{
    private readonly ObservableCollection<AgentRow> _rows = new();
    private readonly ObservableCollection<AddressEntry> _addresses = new();
    private AgentSession? _session;
    private ushort _port;
    private int _sourceCount;

    public SharingStatusPage()
    {
        InitializeComponent();
        SourceList.ItemsSource = _rows;
        AddressList.ItemsSource = _addresses;
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        if (e.Parameter is not ShareRequest req)
        {
            StatusTitle.Text = "No share request.";
            return;
        }

        _port = req.Port;
        RebuildAddresses();
        UpdateTitle();

        _session = AgentSession.Start(req);
        if (_session is null)
        {
            StatusTitle.Text = "Failed to start sharing (see log).";
            return;
        }
        _session.Bound += OnBound;
        _session.RowsChanged += OnRows;
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e) => StopSession();

    // --- Event từ native (thread nền) → marshal về UI thread ---

    private void OnBound(ushort port) => DispatcherQueue.TryEnqueue(() =>
    {
        _port = port;
        RebuildAddresses();
        UpdateTitle();
    });

    private void OnRows(IReadOnlyList<AgentRow> rows) => DispatcherQueue.TryEnqueue(() =>
    {
        _rows.Clear();
        foreach (var r in rows) _rows.Add(r);
        _sourceCount = rows.Count;
        UpdateTitle();
    });

    private void UpdateTitle()
        => StatusTitle.Text = $"Sharing {_sourceCount} source(s) on UDP port {_port}";

    private void RebuildAddresses()
    {
        _addresses.Clear();
        try
        {
            foreach (var ip in NativeMethods.ListLocalIps())
            {
                string addr = $"{ip.Ip}:{_port}";
                _addresses.Add(new AddressEntry($"{addr}    ({ip.AdapterName})", addr));
            }
        }
        catch (DllNotFoundException)
        {
            _addresses.Add(new AddressEntry("deskhub_native.dll not found — build cpp/ first.", ""));
        }
    }

    // --- Nút ---

    private void OnCopyClick(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement fe && fe.Tag is string text && !string.IsNullOrEmpty(text))
        {
            var dp = new DataPackage();
            dp.SetText(text);
            Clipboard.SetContent(dp);
        }
    }

    private void OnStopSourceClick(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement fe && fe.Tag is byte id)
            _session?.Remove(id);
    }

    private async void OnAddSourceClick(object sender, RoutedEventArgs e)
    {
        if (_session is null) return;

        var list = new ListView { SelectionMode = ListViewSelectionMode.Single, Height = 320 };
        try
        {
            foreach (var w in NativeMethods.ListWindows()) list.Items.Add(w);
        }
        catch (Exception ex)
        {
            list.Items.Add("Failed to list windows: " + ex.Message);
        }
        list.DisplayMemberPath = nameof(CaptureWindow.DisplayTitle);

        var dialog = new ContentDialog
        {
            Title = "Add a source",
            Content = list,
            PrimaryButtonText = "Add",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
            XamlRoot = XamlRoot,
        };
        if (await dialog.ShowAsync() == ContentDialogResult.Primary
            && list.SelectedItem is CaptureWindow win)
        {
            _session.AddWindow(win.Hwnd, win.DisplayTitle);
        }
    }

    private void OnStopSharingClick(object sender, RoutedEventArgs e)
    {
        StopSession();
        if (Frame.CanGoBack) Frame.BackStack.Clear();
        Frame.Navigate(typeof(HomePage));
    }

    private void StopSession()
    {
        _session?.Dispose();
        _session = null;
    }
}
