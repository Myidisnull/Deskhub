using System;
using System.Collections.Generic;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Windows.Foundation;

namespace Deskhub.Controls;

// =============================================================================
// Sparkline — biểu đồ đường độ trễ, phần vẽ của deskhub::LatencyTrace.
//
// Xuất hiện ở ba chỗ trong bản thiết kế: HUD của màn xem, panel "máy đang xem" của
// màn chia sẻ, và bảng kiểm tra đường truyền. Cả ba đều là cùng một hình: một đường
// mint mảnh trên nền trong suốt, không trục, không lưới, không nhãn.
//
// VÌ SAO KẾ THỪA Grid CHỨ KHÔNG PHẢI Control
//   Control cần ControlTemplate, mà template cho một thứ chỉ có đúng một Polyline là
//   ba lớp bọc thừa. Grid là Panel — thêm con vào là xong, không template, không
//   OnApplyTemplate, không có trạng thái nào để lệch.
//
// THANG ĐỨNG TỰ ĐỘNG THEO DỮ LIỆU ĐANG HIỆN
//   Không neo đáy vào 0. Độ trễ dao động 4–7 ms trên thang 0–7 gần như là một đường
//   thẳng — mà chính cái dao động đó là thứ người dùng mở biểu đồ ra để xem. Thang
//   chạy từ min tới max của đúng dãy đang vẽ, cộng một khoảng đệm để đường không
//   dán vào mép.
//
// LIÊN QUAN: core deskhub::LatencyTrace (nguồn dãy mẫu, 60 cột × 320 ms)
// =============================================================================
public sealed class Sparkline : Grid
{
    private readonly Polyline _line = new() { StrokeThickness = 1.5, StrokeLineJoin = PenLineJoin.Round };
    private IReadOnlyList<double> _values = Array.Empty<double>();

    public Sparkline()
    {
        Children.Add(_line);
        SizeChanged += (_, _) => Redraw();
    }

    public static readonly DependencyProperty StrokeProperty = DependencyProperty.Register(
        nameof(Stroke), typeof(Brush), typeof(Sparkline),
        new PropertyMetadata(null, (d, e) => ((Sparkline)d)._line.Stroke = (Brush)e.NewValue));

    public Brush Stroke
    {
        get => (Brush)GetValue(StrokeProperty);
        set => SetValue(StrokeProperty, value);
    }

    // Dãy mẫu theo thứ tự CŨ → MỚI (đúng thứ tự LatencyTrace::Snapshot trả về), vẽ
    // trái sang phải.
    public void SetValues(IReadOnlyList<double> values)
    {
        _values = values ?? Array.Empty<double>();
        Redraw();
    }

    private void Redraw()
    {
        _line.Points.Clear();
        double w = ActualWidth, h = ActualHeight;
        if (w <= 1 || h <= 1 || _values.Count < 2) return;

        double min = double.MaxValue, max = double.MinValue;
        foreach (var v in _values)
        {
            if (v < min) min = v;
            if (v > max) max = v;
        }
        // Dãy phẳng hoàn toàn (mọi mẫu bằng nhau): span = 0 sẽ chia cho 0. Vẽ nó
        // thành một đường ngang giữa khung — đó đúng là sự thật của dữ liệu.
        double span = max - min;
        if (span < 1e-6)
        {
            _line.Points.Add(new Point(0, h / 2));
            _line.Points.Add(new Point(w, h / 2));
            return;
        }

        const double pad = 0.12; // chừa mép trên/dưới để đường không dán vào viền
        double usable = h * (1 - 2 * pad);
        double step = w / (_values.Count - 1);
        for (int i = 0; i < _values.Count; i++)
        {
            double norm = (_values[i] - min) / span;   // 0 = thấp nhất
            double y = h * pad + (1 - norm) * usable;  // trục y của màn hình hướng xuống
            _line.Points.Add(new Point(i * step, y));
        }
    }
}
