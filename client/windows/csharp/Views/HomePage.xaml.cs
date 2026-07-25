using System;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Deskhub.Controls;
using Deskhub.Interop;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;

namespace Deskhub.Views;

// =============================================================================
// HomePage — xem HomePage.xaml về bố cục.
//
// VÒNG QUÉT
//   dh_discover_scan CHẶN ~1.2 giây mỗi lượt, nên nó chạy trong Task nền và lặp tới
//   khi rời trang. Đây là lý do trang này có CancellationTokenSource: rời trang giữa
//   một lượt quét mà không huỷ thì Task còn sống thêm hơn một giây rồi cập nhật vào
//   các phần tử XAML đã bị tháo.
//
//   Mỗi lượt là một ẢNH CHỤP ĐỘC LẬP — không có trạng thái tích luỹ ở phía C#. Việc
//   gộp nhiều câu trả lời của cùng một máy đã do core làm (deskhub::HostRegistry), nên
//   ở đây chỉ việc thay cả danh sách.
// =============================================================================
public sealed partial class HomePage : Page
{
    private CancellationTokenSource? _scan;
    private int _foundCount;

    public HomePage()
    {
        InitializeComponent();
    }

    // Đăng ký ở OnNavigatedTo chứ KHÔNG ở constructor, để nó đối xứng với chỗ huỷ
    // đăng ký ở OnNavigatedFrom. Frame dựng một Page MỚI mỗi lần điều hướng tới, nên
    // đăng ký trong constructor sẽ chồng thêm một handler cho mỗi lần qua lại — và
    // các handler cũ vẫn chạy, gắn vào những phần tử XAML đã tháo khỏi cây.
    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        AppState.Changed += ApplyLanguage;
        ApplyLanguage();
        StartScanning();
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e)
    {
        AppState.Changed -= ApplyLanguage;
        StopScanning();
    }

    // --- Chữ ---

    private void ApplyLanguage()
    {
        Eyebrow.Text = L.T("machines").ToUpperInvariant();
        Title.Text = L.T("takeDesk");
        Aside.Text = L.T("oneApp");

        TileConnect.Title = L.T("connectTitle");
        TileConnect.Body = L.T("connectBody");
        TileConnect.ActionText = L.T("takeControl").ToUpperInvariant();
        TileShare.Title = L.T("shareTitle");
        TileShare.Body = L.T("shareBody");
        TileShare.ActionText = L.T("startSharing").ToUpperInvariant();

        RecentLabel.Text = L.T("recentConnections").ToUpperInvariant();
        ForgetText.Text = L.T("forgetAll");
        RecentEmpty.Text = L.T("nothingRemembered");
        FoundLabel.Text = L.T("foundOnNetworks").ToUpperInvariant();
        ScanText.Text = L.T("scanning");
        RescanText.Text = L.T("rescan");

        ShareBarText.Text = L.T("shareThisMac");
        ConnectBarText.Text = L.T("connect");

        BuildRecents(); // thẻ mang chữ bên trong nên phải dựng lại theo ngôn ngữ
    }

    private void UpdateStatusLine()
        => StatusLine.Text = string.Format(L.T("reachableFmt"), Recents.All.Count, _foundCount);

    // --- Danh sách ---

    private void BuildRecents()
    {
        RecentList.Items.Clear();
        foreach (var m in Recents.All)
        {
            // Máy đã lưu: ta chưa dò lại nên KHÔNG khẳng định nó đang sống. Chấm để
            // trạng thái "không sống" và không hiện độ trễ bịa ra.
            RecentList.Items.Add(MachineCard.Stacked(
                string.IsNullOrEmpty(m.Name) ? m.Address : m.Name,
                m.Address, m.Link, rtt: "", live: false,
                onClick: () => Connect(m.Address, m.Name)));
        }
        RecentEmpty.Visibility = Recents.All.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        UpdateStatusLine();
    }

    private void ShowFound(IReadOnlyList<FoundHost> hosts)
    {
        FoundList.Items.Clear();
        foreach (var h in hosts)
        {
            FoundList.Items.Add(MachineCard.Row(
                h.Name,
                h.Busy ? $"{h.Address} · busy" : h.Address,
                Recents.GuessLink(h.Address),
                rtt: $"{h.RttMs} ms",
                live: !h.Busy,
                onClick: () => Connect(h.Address, h.Name)));
        }
        _foundCount = hosts.Count;
        UpdateStatusLine();
    }

    // --- Quét ---

    private void StartScanning()
    {
        StopScanning();
        var cts = new CancellationTokenSource();
        _scan = cts;
        _ = ScanLoopAsync(cts.Token);
    }

    private void StopScanning()
    {
        _scan?.Cancel();
        _scan?.Dispose();
        _scan = null;
    }

    private async Task ScanLoopAsync(CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            List<FoundHost> hosts;
            try
            {
                // Task.Run vì lời gọi này CHẶN ~1.2 giây; gọi trên UI thread là treo app.
                hosts = await Task.Run(() => NativeMethods.Scan(47777, 1200), token);
            }
            catch (OperationCanceledException)
            {
                return;
            }
            catch (Exception)
            {
                // Thiếu DLL, không mở được socket, mạng chặn broadcast — không có gì để
                // người dùng làm, và danh sách rỗng đã nói đúng chuyện đó rồi.
                DispatcherQueue.TryEnqueue(() => ScanRing.IsActive = false);
                return;
            }

            if (token.IsCancellationRequested) return;
            DispatcherQueue.TryEnqueue(() =>
            {
                if (!token.IsCancellationRequested) ShowFound(hosts);
            });

            // Nghỉ giữa hai lượt: quét liên tục thì thẻ máy nhấp nháy theo từng lượt, và
            // mạng lúc nào cũng có broadcast của ta trên đó.
            try
            {
                await Task.Delay(2500, token);
            }
            catch (OperationCanceledException)
            {
                return;
            }
        }
    }

    // --- Nút ---

    private void OnRescan(object sender, RoutedEventArgs e) => StartScanning();

    private void OnForgetAll(object sender, RoutedEventArgs e)
    {
        Recents.ForgetAll();
        BuildRecents();
    }

    private void OnConnectTile(object sender, RoutedEventArgs e) => Frame.Navigate(typeof(ConnectPage));
    private void OnShareTile(object sender, RoutedEventArgs e) => Frame.Navigate(typeof(SharePage));

    // Bấm một thẻ máy = mở màn Kết nối với địa chỉ điền sẵn, KHÔNG nối thẳng. Nối
    // thẳng sẽ bỏ qua ô "chỉ xem", mà đó là lựa chọn người ta cần cân nhắc trước khi
    // gõ vào máy người khác.
    private void Connect(string address, string name = "")
    {
        Recents.Remember(address, name, Recents.GuessLink(address));
        Frame.Navigate(typeof(ConnectPage), address);
    }
}
