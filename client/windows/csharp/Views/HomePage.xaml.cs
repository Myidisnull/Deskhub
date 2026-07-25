using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;

namespace Deskhub.Views;

// HomePage — rẽ hai vai: Connect (client) và Share this PC (host).
public sealed partial class HomePage : Page
{
    public HomePage()
    {
        InitializeComponent();
    }

    private void OnConnectClick(object sender, RoutedEventArgs e)
        => Frame.Navigate(typeof(ConnectPage));

    private void OnShareClick(object sender, RoutedEventArgs e)
        => Frame.Navigate(typeof(SharePickerPage));
}
