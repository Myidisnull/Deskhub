using System;
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
        // Nạp tuỳ chọn giao diện TRƯỚC khi dựng cửa sổ: đổi chủ đề sau khi cửa sổ đã
        // hiện lên sẽ thấy một nháy màu.
        AppState.Load();

        // Instance vừa được UAC nâng quyền mang theo phiên share qua dòng lệnh → vào
        // thẳng màn chia sẻ, khỏi bắt chọn nguồn lần hai (xem ElevationHelper).
        var startupShare = ElevationHelper.ParseArgs(Environment.GetCommandLineArgs());
        _window = new MainWindow(startupShare);
        _window.Activate();
    }
}
