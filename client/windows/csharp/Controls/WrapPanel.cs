using System;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;

namespace Deskhub.Controls;

// =============================================================================
// WrapPanel — xếp con theo hàng, xuống dòng khi hết bề ngang.
//
// VÌ SAO PHẢI TỰ VIẾT
//   WinUI không có WrapPanel (WPF có; ở đây chỉ có ItemsWrapGrid, và nó CHỈ dùng được
//   làm ItemsPanel của ListView/GridView ảo hoá). Các lưới thẻ trong bản thiết kế —
//   máy đã dùng, máy tìm thấy, nguồn chia sẻ — đều là "xếp vừa bao nhiêu thì xếp, hết
//   chỗ thì xuống dòng", nên không tránh được.
//
// KÍCH THƯỚC Ô CỐ ĐỊNH, KHÔNG PHẢI THEO NỘI DUNG
//   Bản thiết kế vẽ các thẻ này bằng nhau chằn chặn. Để mỗi thẻ tự co theo tên máy
//   trong nó thì một máy tên dài sẽ đẩy cả hàng lệch, và mắt mất ngay cái lưới. Nên
//   ItemWidth/ItemHeight là bắt buộc phải đặt, và mọi con đều nhận đúng cỡ đó.
// =============================================================================
public sealed class WrapPanel : Panel
{
    public static readonly DependencyProperty ItemWidthProperty = DependencyProperty.Register(
        nameof(ItemWidth), typeof(double), typeof(WrapPanel),
        new PropertyMetadata(200.0, OnLayoutPropertyChanged));

    public double ItemWidth
    {
        get => (double)GetValue(ItemWidthProperty);
        set => SetValue(ItemWidthProperty, value);
    }

    public static readonly DependencyProperty ItemHeightProperty = DependencyProperty.Register(
        nameof(ItemHeight), typeof(double), typeof(WrapPanel),
        new PropertyMetadata(80.0, OnLayoutPropertyChanged));

    public double ItemHeight
    {
        get => (double)GetValue(ItemHeightProperty);
        set => SetValue(ItemHeightProperty, value);
    }

    public static readonly DependencyProperty GapProperty = DependencyProperty.Register(
        nameof(Gap), typeof(double), typeof(WrapPanel),
        new PropertyMetadata(12.0, OnLayoutPropertyChanged));

    public double Gap
    {
        get => (double)GetValue(GapProperty);
        set => SetValue(GapProperty, value);
    }

    private static void OnLayoutPropertyChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        => ((WrapPanel)d).InvalidateMeasure();

    protected override Size MeasureOverride(Size available)
    {
        int count = Children.Count;
        if (count == 0) return new Size(0, 0);

        var cell = new Size(ItemWidth, ItemHeight);
        foreach (var child in Children) child.Measure(cell);

        int perRow = ColumnsFor(available.Width);
        int rows = (count + perRow - 1) / perRow;
        // Bề ngang trả về là bề ngang THẬT SỰ dùng, không phải available: trả available
        // khi chỉ có một thẻ sẽ làm panel chiếm cả hàng và mọi thứ căn phải bị đẩy đi.
        double usedCols = Math.Min(count, perRow);
        return new Size(usedCols * ItemWidth + (usedCols - 1) * Gap,
            rows * ItemHeight + (rows - 1) * Gap);
    }

    protected override Size ArrangeOverride(Size final)
    {
        int perRow = ColumnsFor(final.Width);
        for (int i = 0; i < Children.Count; i++)
        {
            int row = i / perRow, col = i % perRow;
            Children[i].Arrange(new Rect(
                col * (ItemWidth + Gap), row * (ItemHeight + Gap), ItemWidth, ItemHeight));
        }
        return final;
    }

    // Ít nhất một cột: bề ngang 0 (lúc chưa đo xong) hoặc hẹp hơn một thẻ vẫn phải
    // xếp được, nếu không thì chia cho 0 ở perRow.
    private int ColumnsFor(double width)
    {
        if (double.IsInfinity(width) || width <= 0) return Math.Max(1, Children.Count);
        int n = (int)Math.Floor((width + Gap) / (ItemWidth + Gap));
        return Math.Max(1, n);
    }
}
