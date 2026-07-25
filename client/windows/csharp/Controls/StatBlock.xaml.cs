using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Deskhub.Controls;

// StatBlock — xem StatBlock.xaml về bố cục và vì sao con số dùng phông hiển thị.
// `Tone="accent"` tô con số bằng màu mint: dùng cho ĐÚNG MỘT số trong mỗi nhóm — số
// quan trọng nhất. Tô hết thì không còn số nào nổi bật.
public sealed partial class StatBlock : UserControl
{
    public StatBlock()
    {
        InitializeComponent();
    }

    public static readonly DependencyProperty LabelProperty = DependencyProperty.Register(
        nameof(Label), typeof(string), typeof(StatBlock),
        new PropertyMetadata("", (d, e) => ((StatBlock)d).LabelText.Text = (string)e.NewValue));

    public string Label
    {
        get => (string)GetValue(LabelProperty);
        set => SetValue(LabelProperty, value);
    }

    public static readonly DependencyProperty ValueProperty = DependencyProperty.Register(
        nameof(Value), typeof(string), typeof(StatBlock),
        new PropertyMetadata("", (d, e) => ((StatBlock)d).ValueText.Text = (string)e.NewValue));

    public string Value
    {
        get => (string)GetValue(ValueProperty);
        set => SetValue(ValueProperty, value);
    }

    public static readonly DependencyProperty UnitProperty = DependencyProperty.Register(
        nameof(Unit), typeof(string), typeof(StatBlock),
        new PropertyMetadata("", (d, e) => ((StatBlock)d).UnitText.Text = (string)e.NewValue));

    public string Unit
    {
        get => (string)GetValue(UnitProperty);
        set => SetValue(UnitProperty, value);
    }

    public static readonly DependencyProperty AccentProperty = DependencyProperty.Register(
        nameof(Accent), typeof(bool), typeof(StatBlock),
        new PropertyMetadata(false, (d, e) => ((StatBlock)d).ApplyTone((bool)e.NewValue)));

    public bool Accent
    {
        get => (bool)GetValue(AccentProperty);
        set => SetValue(AccentProperty, value);
    }

    private void ApplyTone(bool accent)
    {
        // Style chứ không phải Brush trực tiếp: {ThemeResource} phải phân giải tại
        // phần tử để theo được chủ đề (xem StatusDot.cs).
        ValueText.Style = (Style)Application.Current.Resources[
            accent ? "StatValueAccent" : "StatValuePrimary"];
    }
}
