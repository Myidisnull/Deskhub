using System.Collections.Generic;
using Deskhub.Interop;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media.Animation;
using Microsoft.UI.Xaml.Navigation;
using Windows.System;

namespace Deskhub.Views;

// ConnectPage — xem ConnectPage.xaml về bố cục.
//
// Trang này KHÔNG tự mở phiên: nó thu địa chỉ + lựa chọn "chỉ xem" rồi chuyển sang
// ViewerPage. Mở phiên ngay ở đây thì lúc kết nối hỏng ta đã rời màn nhập, và người
// dùng phải gõ lại địa chỉ từ đầu.
public sealed partial class ConnectPage : Page
{
    public ConnectPage()
    {
        InitializeComponent();
    }

    // Đăng ký ở đây, không ở constructor — xem ghi chú ở HomePage.OnNavigatedTo.
    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        AppState.Changed += ApplyLanguage;
        ApplyLanguage();

        // Bấm một thẻ máy ở màn chính → địa chỉ điền sẵn, con trỏ ở cuối để sửa cổng.
        if (e.Parameter is string addr && !string.IsNullOrWhiteSpace(addr))
            AddressBox.Text = addr;

        AddressBox.Focus(FocusState.Programmatic);
        AddressBox.SelectionStart = AddressBox.Text.Length;
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e)
        => AppState.Changed -= ApplyLanguage;

    private void ApplyLanguage()
    {
        Eyebrow.Text = L.T("clientMode").ToUpperInvariant();
        Title.Text = L.T("connectTitle");
        Hint.Text = L.T("connectHint");
        GoText.Text = L.T("connect");
        ConnectBarText.Text = L.T("connect");
        ViewOnly.Content = L.T("viewOnlyOption");
        AddressBox.PlaceholderText = L.T("addressPlaceholder");

        P1Label.Text = L.T("onOtherMachine").ToUpperInvariant();
        P1Title.Text = L.T("openAndShare");
        P1Body.Text = L.T("shareOrExe");
        P2Label.Text = L.T("whereAddress").ToUpperInvariant();
        P2Title.Text = L.T("printedOnShare");
        P2Body.Text = L.T("onePerInterfaceLong");
        P3Label.Text = L.T("port").ToUpperInvariant();
        PortStat.Label = L.T("udp");
        P3Body.Text = L.T("portChangeable");
    }

    // Viền mint + quầng mint khi đang gõ. Cái Border không biết con nó có tiêu điểm hay
    // không, nên phải nối tay — không có thì ô địa chỉ không hề phản hồi lúc bấm vào,
    // và người dùng không chắc mình đang gõ vào đâu.
    private void OnAddressFocus(object sender, RoutedEventArgs e) => Focused(true);

    private void OnAddressBlur(object sender, RoutedEventArgs e) => Focused(false);

    private void Focused(bool on)
    {
        FieldGlow.IsOn = on;
        ((Storyboard)Resources[on ? "FocusRingIn" : "FocusRingOut"]).Begin();
    }

    private void OnAddressKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key != VirtualKey.Enter) return;
        e.Handled = true;
        Connect();
    }

    private void OnConnect(object sender, RoutedEventArgs e) => Connect();

    private void Connect()
    {
        var addr = AddressBox.Text.Trim();
        if (string.IsNullOrEmpty(addr))
        {
            Error.Text = L.T("connectFailed");
            Error.Visibility = Visibility.Visible;
            return;
        }
        Error.Visibility = Visibility.Collapsed;

        Recents.Remember(addr, "", Recents.GuessLink(addr));
        // Ô ghi "CHỈ XEM" nên nghĩa của nó ngược với sendInput.
        //
        // Đi qua PickPage chứ không thẳng tới Viewer: host chia sẻ nhiều nguồn thì
        // phải cho chọn. PickPage tự nhảy tiếp sang Viewer nếu host chỉ có một nguồn
        // hoặc không biết LIST_SOURCES — không thêm một bước bấm cho ai cả.
        Frame.Navigate(typeof(PickPage),
            new ViewerRequest(addr, SendInput: ViewOnly.IsChecked != true));
    }
}

// Tham số điều hướng sang ViewerPage.
//
// SourceId là nguồn NÀO của host muốn xem. Host chia sẻ nhiều cửa sổ/màn hình cùng
// lúc, mỗi cái một sourceId, và mỗi cặp (client, nguồn) là một phiên độc lập
// (xem Wire.h). Trước đây chỗ này ghi cứng 0 nên client luôn xem đúng nguồn đầu
// tiên — không có đường nào tới các nguồn còn lại. Mặc định vẫn là 0 để mọi chỗ gọi
// cũ giữ nguyên hành vi.
// `Sources` chỉ khác null khi người dùng tick NHIỀU nguồn ở màn chọn: mỗi cặp
// (client, nguồn) là một phiên độc lập nên xem song song không cần gì thêm trên dây.
// Một nguồn thì để null và dùng SourceId — đường một-nguồn là đường chạy 99% thời
// gian, không nên bắt nó đi qua nhánh danh sách.
public sealed record ViewerRequest(string Address, bool SendInput, byte SourceId = 0,
    IReadOnlyList<HostSource>? Sources = null);
