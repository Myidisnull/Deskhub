using System;
using System.Numerics;
using Microsoft.UI.Composition;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Hosting;
using Windows.Foundation;
using Windows.UI;

namespace Deskhub.Controls;

// =============================================================================
// AccentGlow — quầng sáng mint sau thứ đang được rê chuột. Đây là token
// `--glow-accent` của bản thiết kế: `0 0 0 1px mint-line, 0 12px 40px rgba(mint,.14)`.
// Vòng 1px mint là VIỀN của chính phần tử (BorderActiveBrush); lớp này chỉ lo phần
// toả 40px bên ngoài.
//
// VÌ SAO PHẢI DÙNG COMPOSITION CHỨ KHÔNG PHẢI MỘT THUỘC TÍNH XAML
//   XAML không có bóng đổ MÀU. ThemeShadow là bóng đen theo chiều sâu 3D của hệ thống,
//   không đặt được màu và cần dịch phần tử theo trục Z. Muốn đúng thứ bản thiết kế ghi
//   thì phải xuống lớp Composition: một SpriteVisual mang DropShadow, mặt nạ là hình
//   chữ nhật bo góc dựng bằng CompositionVisualSurface.
//
// PHẢI CÓ MẶT NẠ, KHÔNG ĐƯỢC BỎ QUA
//   DropShadow lấy hình từ nội dung của visual. SpriteVisual rỗng thì không có hình để
//   đổ bóng; cho nó một brush đặc thì lại thấy chính cái khối đặc đó. Mặt nạ là cách
//   nói "bóng có hình bo góc 18" mà không vẽ gì lên màn hình.
//
// ĐẶT Ở ĐÂU
//   Cùng ô Grid với phần tử cần quầng và đứng TRƯỚC nó, để nằm dưới theo thứ tự vẽ.
//   Quầng vượt ra ngoài khung phần tử — không đặt trong thứ gì cắt (Clip) sát viền.
//   Trạng thái bật/tắt do VisualState của template lái (Setter Target="Glow.IsOn").
// =============================================================================
public sealed class AccentGlow : Grid
{
    private const float BlurRadius = 40f;
    private const float OffsetY = 12f;
    private static readonly TimeSpan FadeDuration = TimeSpan.FromMilliseconds(140); // dur-instant

    private SpriteVisual? _sprite;
    private DropShadow? _shadow;
    private ShapeVisual? _shape;
    private CompositionVisualSurface? _surface;
    private CompositionRoundedRectangleGeometry? _geometry;

    public AccentGlow()
    {
        // Nằm dưới một phần tử bấm được: nó mà nhận chuột thì phần tử kia mất hover.
        IsHitTestVisible = false;
        Loaded += (_, _) => Build();
        Unloaded += (_, _) => Teardown();
        SizeChanged += (_, e) => Resize(e.NewSize);
    }

    // --- Thuộc tính ---

    public static readonly DependencyProperty IsOnProperty = DependencyProperty.Register(
        nameof(IsOn), typeof(bool), typeof(AccentGlow),
        new PropertyMetadata(false, (d, _) => ((AccentGlow)d).Fade()));

    public bool IsOn
    {
        get => (bool)GetValue(IsOnProperty);
        set => SetValue(IsOnProperty, value);
    }

    // Màu lấy từ {ThemeResource} ở template chứ không đọc trong code: nền sáng dùng
    // mint tối hơn (#17A97C), và chỉ ThemeResource mới phân giải lại khi đổi chủ đề.
    public static readonly DependencyProperty GlowColorProperty = DependencyProperty.Register(
        nameof(GlowColor), typeof(Color), typeof(AccentGlow),
        new PropertyMetadata(default(Color), (d, _) => ((AccentGlow)d).ApplyColor()));

    public Color GlowColor
    {
        get => (Color)GetValue(GlowColorProperty);
        set => SetValue(GlowColorProperty, value);
    }

    // Bằng đúng bo góc của phần tử phía trên. Lệch một chút thì quầng thò ra ở bốn góc.
    public static readonly DependencyProperty RadiusProperty = DependencyProperty.Register(
        nameof(Radius), typeof(double), typeof(AccentGlow),
        new PropertyMetadata(18.0, (d, _) => ((AccentGlow)d).Resize(
            new Size(((AccentGlow)d).ActualWidth, ((AccentGlow)d).ActualHeight))));

    public double Radius
    {
        get => (double)GetValue(RadiusProperty);
        set => SetValue(RadiusProperty, value);
    }

    // Độ đậm đỉnh của quầng — .14 theo token; để lộ ra ngoài vì nền sáng cần đậm hơn
    // mới thấy được trên nền trắng.
    public static readonly DependencyProperty GlowOpacityProperty = DependencyProperty.Register(
        nameof(GlowOpacity), typeof(double), typeof(AccentGlow),
        new PropertyMetadata(0.14, (d, _) => ((AccentGlow)d).ApplyColor()));

    public double GlowOpacity
    {
        get => (double)GetValue(GlowOpacityProperty);
        set => SetValue(GlowOpacityProperty, value);
    }

    // --- Dựng ---

    private void Build()
    {
        if (_sprite != null) return;
        var c = ElementCompositionPreview.GetElementVisual(this).Compositor;

        // Mặt nạ: một hình chữ nhật bo góc TRẮNG ĐẶC, vẽ ra một bề mặt ngoài màn hình.
        // Chỉ kênh alpha của nó được dùng, nên màu trắng ở đây không bao giờ hiện ra.
        _geometry = c.CreateRoundedRectangleGeometry();
        var shape = c.CreateSpriteShape(_geometry);
        shape.FillBrush = c.CreateColorBrush(Microsoft.UI.Colors.White);
        _shape = c.CreateShapeVisual();
        _shape.Shapes.Add(shape);

        _surface = c.CreateVisualSurface();
        _surface.SourceVisual = _shape;

        _shadow = c.CreateDropShadow();
        _shadow.Mask = c.CreateSurfaceBrush(_surface);
        _shadow.BlurRadius = BlurRadius;
        _shadow.Offset = new Vector3(0f, OffsetY, 0f);

        _sprite = c.CreateSpriteVisual();
        _sprite.Shadow = _shadow;
        _sprite.Opacity = IsOn ? 1f : 0f;

        ApplyColor();
        Resize(new Size(ActualWidth, ActualHeight));
        ElementCompositionPreview.SetElementChildVisual(this, _sprite);
    }

    private void Teardown()
    {
        ElementCompositionPreview.SetElementChildVisual(this, null);
        _sprite?.Dispose();
        _shadow?.Dispose();
        _surface?.Dispose();
        _shape?.Dispose();
        _geometry?.Dispose();
        _sprite = null;
        _shadow = null;
        _surface = null;
        _shape = null;
        _geometry = null;
    }

    private void ApplyColor()
    {
        if (_shadow == null) return;
        _shadow.Color = GlowColor;
        _shadow.Opacity = (float)GlowOpacity;
    }

    private void Resize(Size size)
    {
        if (_sprite == null) return;
        var v = new Vector2((float)size.Width, (float)size.Height);
        _sprite.Size = v;
        _shape!.Size = v;
        _surface!.SourceSize = v;
        _geometry!.Size = v;
        _geometry.CornerRadius = new Vector2((float)Radius);
    }

    // 140ms, cùng đường cong với mọi chuyển màu khác (tokens/motion.css). Bật/tắt phựt
    // một cái là thứ duy nhất trong hệ này nhảy giật — bản thiết kế ghi "quick and quiet".
    private void Fade()
    {
        if (_sprite == null) return;
        var c = _sprite.Compositor;
        var a = c.CreateScalarKeyFrameAnimation();
        a.InsertKeyFrame(1f, IsOn ? 1f : 0f,
            c.CreateCubicBezierEasingFunction(new Vector2(.4f, 0f), new Vector2(.2f, 1f)));
        a.Duration = FadeDuration;
        _sprite.StartAnimation("Opacity", a);
    }
}
