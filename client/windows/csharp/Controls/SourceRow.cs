using System;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Deskhub.Controls;

// =============================================================================
// SourceRow — một dòng "cửa sổ hoặc màn hình" trong màn chia sẻ.
//
//   [tick] [biểu tượng] Tên nguồn                    2560×1600  [huy hiệu trạng thái]
//
// BẤM CẢ DÒNG HAY CHỈ BẤM Ô TICK — TUỲ MÀN, VÀ RANH GIỚI LÀ "CÓ ĐANG CHẠY KHÔNG"
//   Màn CHIA SẺ (rowToggles: false): chỉ ô tick nhận bấm. Dòng ở đó có thể đang truyền
//   hình cho người khác, và bấm nhầm vào tên nguồn mà cắt luôn phiên của họ là một cú
//   bấm rất đắt để hoàn tác.
//   Màn CHỌN NGUỒN (rowToggles: true): bấm đâu trong dòng cũng tick. Ở đó chưa có phiên
//   nào cả, tick sai thì tick lại — không có gì để mà hỏng, nên bắt người dùng nhắm vào
//   một ô 18px là làm khó không đổi lấy gì.
//
//   Khi cả dòng nhận bấm thì ô tick phải TẮT hit-test: để nguyên thì cú bấm trúng ô
//   tick sẽ chạy hai lần (một lần cho ô, một lần cho dòng) và trạng thái quay về chỗ cũ
//   — lỗi trông như "bấm vào ô tick thì không ăn".
//
// NÚT "DỪNG" CHỈ HIỆN Ở NGUỒN ĐANG TRUYỀN
//   Bản thiết kế đặt nó ngay cạnh pill trạng thái. Bỏ tick cũng ra cùng kết quả, nhưng
//   giữa lúc đang chia sẻ thì "dừng cái này" là một câu lệnh, còn "bỏ tick" là một
//   thao tác chỉnh danh sách — người đang vội cần thấy đúng cái nút mang chữ đó.
//   Nguồn chưa vào phiên thì không có nút: dừng một thứ chưa chạy là câu vô nghĩa.
// =============================================================================
public static class SourceRow
{
    public static UIElement Build(string name, string detail, string kindLabel,
        bool isDisplay, bool selected, string state, StatePill.Tone tone,
        bool canStop, string stopLabel, Action<bool> onToggle, Action onStop,
        bool rowToggles = false)
    {
        var grid = new Grid { ColumnSpacing = 12, VerticalAlignment = VerticalAlignment.Center };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto }); // tick
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto }); // icon
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto }); // trạng thái

        var check = new CheckBox
        {
            IsChecked = selected,
            Style = (Style)Application.Current.Resources["DhCheckBox"],
            VerticalAlignment = VerticalAlignment.Center,
        };
        if (rowToggles)
        {
            // Ô tick thành thứ chỉ để NHÌN; cả dòng mới là cái nút.
            check.IsHitTestVisible = false;
        }
        else
        {
            check.Checked += (_, _) => onToggle(true);
            check.Unchecked += (_, _) => onToggle(false);
        }
        Grid.SetColumn(check, 0);
        grid.Children.Add(check);

        // Màn hình và cửa sổ có hệ quả riêng tư khác hẳn nhau, nên chúng phải phân
        // biệt được bằng MẮT chứ không chỉ bằng tên.
        var icon = new FontIcon
        {
            FontFamily = new Microsoft.UI.Xaml.Media.FontFamily("Segoe Fluent Icons"),
            Glyph = isDisplay ? "" : "",
            FontSize = 17,
            VerticalAlignment = VerticalAlignment.Center,
        };
        icon.SetValue(ToolTipService.ToolTipProperty, kindLabel);
        Grid.SetColumn(icon, 1);
        grid.Children.Add(icon);

        var mid = new StackPanel { Spacing = 3, VerticalAlignment = VerticalAlignment.Center };
        mid.Children.Add(new TextBlock
        {
            Text = name,
            Style = (Style)Application.Current.Resources["RowTitleText"],
        });
        mid.Children.Add(new TextBlock
        {
            Text = detail,
            Style = (Style)Application.Current.Resources["MonoText"],
        });
        Grid.SetColumn(mid, 2);
        grid.Children.Add(mid);

        var trailing = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            Spacing = 10,
            VerticalAlignment = VerticalAlignment.Center,
        };
        trailing.Children.Add(StatePill.Build(state, tone));
        if (canStop)
        {
            var stop = new Button
            {
                Style = (Style)Application.Current.Resources["SecondaryButtonSm"],
                Content = new TextBlock { Text = stopLabel },
                VerticalAlignment = VerticalAlignment.Center,
            };
            stop.Click += (_, _) => onStop();
            trailing.Children.Add(stop);
        }
        Grid.SetColumn(trailing, 3);
        grid.Children.Add(trailing);

        if (!rowToggles)
        {
            return new Border
            {
                Style = (Style)Application.Current.Resources["RowBorder"],
                Child = grid,
            };
        }

        // CardButton chứ không phải SecondaryButton: rê chuột vào là viền đổi sang mint,
        // đúng tín hiệu "bấm được vào đây" mà thẻ máy ở màn chính đang dùng — và cũng
        // đúng thứ `SourceRow` của bản thiết kế làm khi nó được truyền `onClick`.
        var row = new Button
        {
            Style = (Style)Application.Current.Resources["CardButton"],
            Content = grid,
            Padding = new Thickness(14, 12, 14, 12),
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Stretch,
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            VerticalContentAlignment = VerticalAlignment.Center,
            Height = double.NaN,
        };
        row.Click += (_, _) => onToggle(!selected);
        return row;
    }
}
