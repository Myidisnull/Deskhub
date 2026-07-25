using System;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Deskhub.Controls;

// =============================================================================
// MachineCard — thẻ một máy. Hai bố cục, đúng như `MachineCard` của bản thiết kế:
//
//   Stacked — tên trên, địa chỉ dưới, chấm + độ trễ ở góc. Dùng cho "gần đây": ít
//             dòng, mỗi dòng đáng một ô rộng rãi.
//   Row     — tất cả trên một hàng. Dùng cho "tìm thấy trong mạng": danh sách này
//             dài và đổi liên tục, hàng thấp thì thấy được nhiều máy cùng lúc.
//
// VÌ SAO DỰNG BẰNG CODE CHỨ KHÔNG PHẢI DataTemplate
//   DataTemplate cần một lớp dữ liệu có binding cho từng trường, cộng converter cho
//   những chỗ như "ẩn độ trễ khi rỗng". Ở đây thẻ được dựng lại nguyên khối mỗi khi
//   dữ liệu đổi (mỗi lượt quét), nên binding không mang lại gì mà lại thêm hai lớp
//   phải đọc để hiểu một cái thẻ.
// =============================================================================
public static class MachineCard
{
    public static UIElement Stacked(string name, string address, string link, string rtt,
        bool live, Action onClick)
    {
        var top = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        top.Children.Add(new StatusDot { IsLive = live });
        top.Children.Add(Text(name, "RowTitleText"));

        var body = new StackPanel { Spacing = 6 };
        body.Children.Add(top);
        body.Children.Add(Text(address, "MonoText"));
        body.Children.Add(Meta(link, rtt));

        return Clickable(body, onClick);
    }

    public static UIElement Row(string name, string address, string link, string rtt,
        bool live, Action onClick)
    {
        var grid = new Grid { ColumnSpacing = 12, VerticalAlignment = VerticalAlignment.Center };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

        var dot = new StatusDot { IsLive = live };
        Grid.SetColumn(dot, 0);
        grid.Children.Add(dot);

        var mid = new StackPanel { Spacing = 3, VerticalAlignment = VerticalAlignment.Center };
        mid.Children.Add(Text(name, "RowTitleText"));
        mid.Children.Add(Text(address, "MonoText"));
        Grid.SetColumn(mid, 1);
        grid.Children.Add(mid);

        var right = Meta(link, rtt);
        right.HorizontalAlignment = HorizontalAlignment.Right;
        right.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(right, 2);
        grid.Children.Add(right);

        return Clickable(grid, onClick);
    }

    // Hàng phụ bên phải: loại đường (chip) + độ trễ. Trường rỗng thì KHÔNG vẽ gì —
    // một chip trống hay chữ "— ms" đều là nhiễu, và độ trễ ta chưa đo thì đừng bịa.
    private static StackPanel Meta(string link, string rtt)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        if (!string.IsNullOrEmpty(link)) row.Children.Add(Chip(link));
        if (!string.IsNullOrEmpty(rtt))
        {
            var t = Text(rtt, "MonoText");
            t.VerticalAlignment = VerticalAlignment.Center;
            row.Children.Add(t);
        }
        return row;
    }

    private static Border Chip(string text)
    {
        var b = new Border { Style = (Style)Application.Current.Resources["ChipBorder"] };
        b.Child = Text(text, "MonoText");
        return b;
    }

    private static TextBlock Text(string s, string styleKey) => new()
    {
        Text = s,
        Style = (Style)Application.Current.Resources[styleKey],
    };

    // Cả thẻ là một nút: vùng bấm bằng đúng vùng nhìn thấy. Một thẻ có "chỗ bấm" nhỏ
    // hơn chính nó là thứ người dùng không đoán được và không thấy được.
    //
    // CardButton chứ không phải SecondaryButton: bản thiết kế cho thẻ máy đổi viền
    // sang MINT khi rê chuột — viền mint là tín hiệu "bấm được vào đây" và chỉ thẻ
    // mới có; nút thường chỉ nâng nền kính lên một nấc.
    private static Button Clickable(FrameworkElement content, Action onClick)
    {
        var b = new Button
        {
            Style = (Style)Application.Current.Resources["CardButton"],
            Content = content,
            Padding = new Thickness(16, 14, 16, 14),
            CornerRadius = (CornerRadius)Application.Current.Resources["RadiusLg"],
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            VerticalContentAlignment = VerticalAlignment.Center,
            Height = double.NaN,
        };
        b.Click += (_, _) => onClick();
        return b;
    }
}
