using System;
using Deskhub.Interop;
using Deskhub.Views;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Windows.UI;

namespace Deskhub;

// =============================================================================
// MainWindow — vỏ app: rail + Frame. Xem MainWindow.xaml về bố cục.
//
// BA VIỆC CODE-BEHIND NÀY LÀM
//   1. Áp chủ đề. RequestedTheme của Application chỉ đọc được lúc khởi động, nên nút
//      sáng/tối phải đặt RequestedTheme trên phần tử GỐC (Root) — mọi {ThemeResource}
//      bên dưới tự phân giải lại. Title bar là cửa sổ hệ thống, không nằm trong cây
//      XAML, nên phải tô tay cho khớp.
//   2. Chuyển trang từ rail, và tô sáng mục đang mở.
//   3. Ẩn rail ở màn xem (bản thiết kế vẽ màn đó chiếm trọn cửa sổ).
// =============================================================================
public sealed partial class MainWindow : Window
{
    public MainWindow(ShareRequest? startupShare = null)
    {
        InitializeComponent();

        // 1280×840 là đúng khung bản thiết kế vẽ mọi màn desktop trong đó.
        AppWindow.Resize(new Windows.Graphics.SizeInt32(1280, 840));
        CenterOnScreen();

        AppState.Changed += ApplyAppState;
        ApplyAppState();

        // Bàn giao từ instance elevated → vào thẳng màn đang chia sẻ; còn lại mở Home.
        if (startupShare is not null)
            RootFrame.Navigate(typeof(SharePage), startupShare);
        else
            RootFrame.Navigate(typeof(HomePage));
    }

    // Chủ đề + ngôn ngữ. Gọi lúc khởi động và mỗi lần người dùng bấm hai nút ở chân rail.
    private void ApplyAppState()
    {
        Root.RequestedTheme = AppState.Theme;

        // Mặt trời khi đang tối (bấm để sang sáng), mặt trăng khi đang sáng.
        ThemeIcon.Glyph = AppState.IsDark ? "" : "";
        LangText.Text = AppState.Lang.ToUpperInvariant();

        // Nút chỉ có biểu tượng thì KHÔNG có gì để đọc lên: trình đọc màn hình gặp
        // chúng chỉ nói "button". Đặt cả tooltip (cho mắt) lẫn AutomationProperties.Name
        // (cho trình đọc màn hình — và nhờ đó kiểm thử tự động cũng bấm được đúng nút).
        Label(NavHome, L.T("machines"));
        Label(NavConnect, L.T("connect"));
        Label(NavShare, L.T("shareThisMac"));
        Label(ThemeButton, L.T(AppState.IsDark ? "appearanceLight" : "appearanceDark"));
        Label(LangButton, AppState.Lang.ToUpperInvariant());

        // Title bar không phải phần tử XAML nên không theo RequestedTheme — tô tay.
        var bg = AppState.IsDark
            ? Color.FromArgb(255, 0x07, 0x09, 0x0C)
            : Color.FromArgb(255, 0xF6, 0xF8, 0xFA);
        var fg = AppState.IsDark
            ? Color.FromArgb(255, 0xF2, 0xF5, 0xF7)
            : Color.FromArgb(255, 0x0E, 0x15, 0x1C);
        var tb = AppWindow.TitleBar;
        tb.BackgroundColor = bg;
        tb.InactiveBackgroundColor = bg;
        tb.ButtonBackgroundColor = bg;
        tb.ButtonInactiveBackgroundColor = bg;
        tb.ForegroundColor = fg;
        tb.ButtonForegroundColor = fg;
    }

    private void OnToggleTheme(object sender, RoutedEventArgs e) => AppState.ToggleTheme();
    private void OnToggleLang(object sender, RoutedEventArgs e) => AppState.ToggleLang();

    private void OnNavHome(object sender, RoutedEventArgs e) => Go(typeof(HomePage));
    private void OnNavConnect(object sender, RoutedEventArgs e) => Go(typeof(ConnectPage));
    private void OnNavShare(object sender, RoutedEventArgs e) => Go(typeof(SharePage));

    private void Go(Type page)
    {
        if (RootFrame.CurrentSourcePageType == page) return;
        RootFrame.Navigate(page);
    }

    private void OnFrameNavigated(object sender, NavigationEventArgs e)
    {
        // Màn xem chiếm trọn cửa sổ: hình từ máy kia là nội dung, mọi điều khiển nằm
        // trong HUD nổi trên nó.
        bool viewer = e.SourcePageType == typeof(ViewerPage);
        Rail.Visibility = viewer ? Visibility.Collapsed : Visibility.Visible;
        RailColumn.Width = viewer ? new GridLength(0) : new GridLength(74);
        Glow.Visibility = viewer ? Visibility.Collapsed : Visibility.Visible;

        // Mục đang mở: nền rửa mint + biểu tượng mint (style IconButtonActive).
        SetNav(NavHome, e.SourcePageType == typeof(HomePage));
        SetNav(NavConnect, e.SourcePageType == typeof(ConnectPage));
        SetNav(NavShare, e.SourcePageType == typeof(SharePage));
    }

    // Tên hiển thị + tên cho trình đọc màn hình, đặt cùng lúc để chúng không lệch nhau.
    private static void Label(Button b, string text)
    {
        b.SetValue(ToolTipService.ToolTipProperty, text);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(b, text);
    }

    private static void SetNav(Button b, bool active)
        => b.Style = (Style)Application.Current.Resources[active ? "IconButtonActive" : "IconButton"];

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
