using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Deskhub.Controls;

// =============================================================================
// Tile — ô lớn ở màn chính ("Connect to a machine" / "Share this PC"). Dựng theo
// `Tile` của bản thiết kế: CẢ Ô là cái nút, không có nút con bên trong; dòng cuối
// là một dòng chữ mono viết HOA đóng vai "nút", không phải một <Button> thật.
//
// VÌ SAO LÀ MỘT LỚP RIÊNG CHỨ KHÔNG PHẢI <Button> + Style
//   Rê chuột vào ô phải đổi màu HAI PHẦN TỬ NẰM TRONG nội dung: biểu tượng và dòng
//   hành động, cả hai chuyển sang mint. Setter của VisualState chỉ với tới được phần
//   tử có tên TRONG CÙNG template — nội dung viết ở trang nằm ở namescope khác, đặt
//   x:Name cho nó cũng không tới được. Đưa hẳn biểu tượng/chữ vào template rồi cho
//   trang truyền vào bằng thuộc tính là cách duy nhất để trạng thái chạm tới chúng.
//
//   Kế thừa Button (chứ không phải Control) để giữ nguyên Click, bàn phím, và nhóm
//   trạng thái "CommonStates" mà VSM đã tự lái sẵn.
//
// Phần vỏ nằm ở style "TileButton" trong Themes/Controls.xaml.
// =============================================================================
public sealed class Tile : Button
{
    // Ký tự trong phông Segoe Fluent Icons. Bản thiết kế dùng Lucide; Windows không
    // có bộ đó nên đổi sang phông biểu tượng hệ thống (xem ghi chú thay thế ở readme
    // của bản thiết kế: đi qua một chỗ duy nhất để sau này đổi một lần là xong).
    public static readonly DependencyProperty GlyphProperty = DependencyProperty.Register(
        nameof(Glyph), typeof(string), typeof(Tile), new PropertyMetadata(""));

    public string Glyph
    {
        get => (string)GetValue(GlyphProperty);
        set => SetValue(GlyphProperty, value);
    }

    public static readonly DependencyProperty TitleProperty = DependencyProperty.Register(
        nameof(Title), typeof(string), typeof(Tile), new PropertyMetadata(""));

    public string Title
    {
        get => (string)GetValue(TitleProperty);
        set => SetValue(TitleProperty, value);
    }

    public static readonly DependencyProperty BodyProperty = DependencyProperty.Register(
        nameof(Body), typeof(string), typeof(Tile), new PropertyMetadata(""));

    public string Body
    {
        get => (string)GetValue(BodyProperty);
        set => SetValue(BodyProperty, value);
    }

    // Dòng cuối ("TAKE CONTROL →"). Chữ HOA do NƠI GỌI đặt, không phải template —
    // XAML không có textTransform, và tiếng Việt có dấu cần ToUpperInvariant() của
    // .NET chứ không phải một bảng chữ hoa tự chế.
    public static readonly DependencyProperty ActionTextProperty = DependencyProperty.Register(
        nameof(ActionText), typeof(string), typeof(Tile), new PropertyMetadata(""));

    public string ActionText
    {
        get => (string)GetValue(ActionTextProperty);
        set => SetValue(ActionTextProperty, value);
    }
}
