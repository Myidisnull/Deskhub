using Microsoft.UI.Xaml;

namespace Deskhub;

// =============================================================================
// App — điểm vào ứng dụng WinUI3. Chỉ tạo và hiện cửa sổ chính; mọi điều hướng nằm
// trong MainWindow (Frame). Với app unpackaged (WindowsPackageType=None), Windows App
// SDK tự khởi tạo bootstrap trước khi tới đây.
// =============================================================================
public partial class App : Application
{
    private Window? _window;

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _window = new MainWindow();
        _window.Activate();
    }
}
