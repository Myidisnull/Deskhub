using System;
using Microsoft.UI.Xaml;
using Windows.Storage;

namespace Deskhub;

// =============================================================================
// AppState — hai tuỳ chọn toàn app mà bản thiết kế đặt ở CHÂN THANH RAIL, cạnh nhau:
// giao diện sáng/tối và ngôn ngữ EN/VI. Đối chiếu với `APP` trong i18n.jsx của dự
// án thiết kế (một store bé + danh sách người đăng ký).
//
// VÌ SAO LÀ STATIC CHỨ KHÔNG PHẢI DI
//   Đúng một cái tồn tại trong đời sống của tiến trình, và mọi trang đều cần. Bọc nó
//   vào container rồi truyền qua constructor của từng Page chỉ thêm ống dẫn mà không
//   thêm khả năng gì — Page trong WinUI được framework dựng, không phải ta.
//
// LƯU LẠI GIỮA CÁC LẦN CHẠY
//   ApplicationData.LocalSettings. App chạy UNPACKAGED (WindowsPackageType=None) nên
//   API này chỉ hoạt động khi Windows App SDK đã cấp danh tính — trên bản dựng
//   unpackaged nó ném ngoại lệ. Vì thế mọi truy cập đều bọc try/catch và rơi về mặc
//   định: mất tuỳ chọn đã lưu là phiền, còn sập lúc khởi động là hỏng.
//
// LIÊN QUAN: Strings.cs (bảng chữ), Shell.xaml.cs (nút bấm + áp RequestedTheme)
// =============================================================================
public static class AppState
{
    private const string ThemeKey = "appearance";
    private const string LangKey = "language";

    private static ElementTheme _theme = ElementTheme.Dark;
    private static string _lang = "en";

    // Bắn khi theme HOẶC ngôn ngữ đổi. Shell nghe để áp lại RequestedTheme; mỗi trang
    // nghe để vẽ lại chữ. Không tách hai sự kiện: cả hai đều dẫn tới "vẽ lại màn này".
    public static event Action? Changed;

    public static ElementTheme Theme
    {
        get => _theme;
        set
        {
            if (_theme == value) return;
            _theme = value;
            Save(ThemeKey, value == ElementTheme.Light ? "light" : "dark");
            Changed?.Invoke();
        }
    }

    // "en" hoặc "vi".
    public static string Lang
    {
        get => _lang;
        set
        {
            var v = value == "vi" ? "vi" : "en";
            if (_lang == v) return;
            _lang = v;
            Save(LangKey, v);
            Changed?.Invoke();
        }
    }

    public static bool IsDark => _theme != ElementTheme.Light;

    public static void ToggleTheme()
        => Theme = IsDark ? ElementTheme.Light : ElementTheme.Dark;

    public static void ToggleLang()
        => Lang = _lang == "en" ? "vi" : "en";

    // Nạp tuỳ chọn đã lưu. Gọi một lần lúc khởi động, TRƯỚC khi dựng cửa sổ — đổi
    // theme sau khi cửa sổ hiện lên sẽ thấy một nháy màu.
    public static void Load()
    {
        if (Read(ThemeKey) is string t)
            _theme = t == "light" ? ElementTheme.Light : ElementTheme.Dark;
        if (Read(LangKey) is string l)
            _lang = l == "vi" ? "vi" : "en";
    }

    private static string? Read(string key)
    {
        try
        {
            return ApplicationData.Current.LocalSettings.Values[key] as string;
        }
        catch (Exception)
        {
            return null; // chạy unpackaged, không có kho settings — dùng mặc định
        }
    }

    private static void Save(string key, string value)
    {
        try
        {
            ApplicationData.Current.LocalSettings.Values[key] = value;
        }
        catch (Exception)
        {
            // Không lưu được thì thôi; tuỳ chọn vẫn có tác dụng trong phiên này.
        }
    }
}
