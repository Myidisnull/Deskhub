using System;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Deskhub.Controls;

// =============================================================================
// AddressRow — một địa chỉ để đọc cho máy bên kia: "192.168.1.7:47777  WI-FI  [chép]".
//
// CỠ CHỮ LỚN HƠN MỌI CHỖ KHÁC LÀ CÓ CHỦ Ý
//   Con số này thường được đọc TO cho người ở phòng khác, hoặc gõ lại bằng tay trên
//   điện thoại. Nó không phải siêu dữ liệu; ở màn chia sẻ nó gần như là nội dung
//   chính. Bản thiết kế đặt riêng --text-mono: 16px cho panel này.
//
// NÚT CHÉP LUÔN HIỆN, KHÔNG PHẢI CHỈ KHI RÊ CHUỘT
//   Nút hiện-khi-hover thì không ai biết là có, và ở đây nó là đường tắt duy nhất
//   thay cho việc chép tay một chuỗi số.
//
// KÝ TỰ BIỂU TƯỢNG VIẾT BẰNG "\uXXXX", KHÔNG DÁN KÝ TỰ THẲNG VÀO
//   Vùng Private Use Area của Segoe Fluent Icons không sống sót qua mọi lần lưu file:
//   dán ký tự thẳng vào rồi một công cụ nào đó ghi lại file bằng bảng mã khác là nó
//   biến mất, để lại Glyph = "" — nút vẫn còn đó, vẫn bấm được, chỉ là VÔ HÌNH. Không
//   có lỗi biên dịch, không có cảnh báo; đúng cái đã xảy ra với nút này.
// =============================================================================
public static class AddressRow
{
    private const string CopyGlyph = "\uE8C8"; // Segoe Fluent Icons: Copy

    public static UIElement Build(string address, string adapter, Action<string> onCopy)
    {
        var grid = new Grid { ColumnSpacing = 12, VerticalAlignment = VerticalAlignment.Center };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

        var addr = Text(address, "AddressText");
        Grid.SetColumn(addr, 0);
        grid.Children.Add(addr);

        // Nhãn đường truyền là CHỮ, không phải chip. Chip là để đánh dấu một trạng thái;
        // ở đây "Wi-Fi" chỉ nói địa chỉ này thuộc card mạng nào — bản thiết kế viết nó
        // như một eyebrow mono viết hoa đứng cạnh số.
        var link = Text(adapter.ToUpperInvariant(), "EyebrowDimText");
        link.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(link, 1);
        grid.Children.Add(link);

        var copy = new Button
        {
            Style = (Style)Application.Current.Resources["CopyIconButton"],
            Content = new FontIcon
            {
                FontFamily = new FontFamily("Segoe Fluent Icons"),
                Glyph = CopyGlyph,
                FontSize = 15,
            },
        };
        ToolTipService.SetToolTip(copy, L.T("copy"));
        copy.Click += (_, _) => onCopy(address);
        Grid.SetColumn(copy, 2);
        grid.Children.Add(copy);

        return new Border
        {
            Style = (Style)Application.Current.Resources["RowBorder"],
            CornerRadius = (CornerRadius)Application.Current.Resources["RadiusMd"],
            Padding = new Thickness(16, 10, 10, 10),
            Child = grid,
        };
    }

    // Style thay vì gán brush trong code: brush đọc từ Application.Current.Resources
    // luôn là brush của chủ đề LÚC KHỞI ĐỘNG (xem ghi chú dài ở StatusDot.cs).
    private static TextBlock Text(string s, string styleKey) => new()
    {
        Text = s,
        Style = (Style)Application.Current.Resources[styleKey],
    };
}
