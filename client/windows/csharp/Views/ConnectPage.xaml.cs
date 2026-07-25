using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.System;

namespace Deskhub.Views;

// ConnectPage — nhập địa chỉ host rồi mở phiên xem. Phiên xem thật (video vào
// SwapChainPanel) là M3; M1 chỉ xác thực đầu vào và báo trạng thái.
public sealed partial class ConnectPage : Page
{
    public ConnectPage()
    {
        InitializeComponent();
    }

    private void OnAddressKeyDown(object sender, Microsoft.UI.Xaml.Input.KeyRoutedEventArgs e)
    {
        if (e.Key == VirtualKey.Enter) OnConnectClick(sender, e);
    }

    private void OnConnectClick(object sender, RoutedEventArgs e)
    {
        var addr = AddressBox.Text.Trim();
        if (string.IsNullOrEmpty(addr))
        {
            Show(InfoBarSeverity.Warning, "Enter an IP address first.");
            return;
        }
        Frame.Navigate(typeof(ViewerPage), addr);
    }

    private void OnBackClick(object sender, RoutedEventArgs e)
    {
        if (Frame.CanGoBack) Frame.GoBack();
        else Frame.Navigate(typeof(HomePage));
    }

    private void Show(InfoBarSeverity severity, string message)
    {
        Info.Severity = severity;
        Info.Message = message;
        Info.IsOpen = true;
    }
}
