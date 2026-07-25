using Deskhub.Interop;
using Deskhub.Views;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Windows.UI;

namespace Deskhub;

// =============================================================================
// MainWindow — cửa sổ chính, chỉ chứa một Frame điều hướng. Chỉnh title bar sang tối
// và đặt kích thước/căn giữa như cửa sổ Mac, rồi mở màn Home.
// =============================================================================
public sealed partial class MainWindow : Window
{
    public MainWindow(ShareRequest? startupShare = null)
    {
        InitializeComponent();

        var titleBar = AppWindow.TitleBar;
        // Title bar tối để khớp nền (không thì thanh tiêu đề trắng lệch hẳn với ảnh Mac).
        titleBar.BackgroundColor = Color.FromArgb(255, 0x1C, 0x1C, 0x1E);
        titleBar.InactiveBackgroundColor = Color.FromArgb(255, 0x1C, 0x1C, 0x1E);
        titleBar.ButtonBackgroundColor = Color.FromArgb(255, 0x1C, 0x1C, 0x1E);
        titleBar.ButtonInactiveBackgroundColor = Color.FromArgb(255, 0x1C, 0x1C, 0x1E);
        titleBar.ForegroundColor = Colors.White;
        titleBar.ButtonForegroundColor = Colors.White;

        // Kích thước gần với cửa sổ Mac trong ảnh; căn giữa màn hình hiện tại.
        AppWindow.Resize(new Windows.Graphics.SizeInt32(1000, 720));
        CenterOnScreen();

        // Bàn giao từ instance elevated → vào thẳng màn đang-share; còn lại mở Home.
        if (startupShare is not null)
            RootFrame.Navigate(typeof(SharingStatusPage), startupShare);
        else
            RootFrame.Navigate(typeof(HomePage));
    }

    private void CenterOnScreen()
    {
        var area = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Nearest);
        if (area is null) return;
        var centered = AppWindow.Position;
        centered.X = area.WorkArea.X + (area.WorkArea.Width - AppWindow.Size.Width) / 2;
        centered.Y = area.WorkArea.Y + (area.WorkArea.Height - AppWindow.Size.Height) / 2;
        AppWindow.Move(centered);
    }
}
