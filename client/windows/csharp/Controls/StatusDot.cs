using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Shapes;

namespace Deskhub.Controls;

// =============================================================================
// StatusDot — chấm 8px "đang sống / không". Nhỏ nhất nhưng xuất hiện nhiều nhất
// trong bản thiết kế: mỗi thẻ máy, mỗi dòng nguồn, mỗi HUD đều có một cái.
//
// CHẤM SỐNG CÓ QUẦNG SÁNG, CHẤM CHẾT THÌ KHÔNG
//   Đây không phải trang trí. Cả hai trạng thái đều là một hình tròn cùng cỡ; nếu chỉ
//   khác màu thì người mù màu xanh–đỏ (khoảng 8% nam giới) không phân biệt được.
//   Quầng sáng thêm một tín hiệu thứ hai — kích thước và độ chói — mà ai cũng thấy.
//
// VÌ SAO ĐỔI MÀU BẰNG Style CHỨ KHÔNG GÁN Brush TRONG CODE
//   Chủ đề sáng/tối được đặt bằng RequestedTheme trên phần tử gốc, nên {ThemeResource}
//   phải được phân giải TẠI PHẦN TỬ mới ra đúng brush. Đọc
//   Application.Current.Resources["..."] trong code sẽ luôn trả brush của chủ đề khởi
//   động và không bao giờ đổi — một lỗi âm thầm: giao diện vẫn chạy, chỉ là chấm giữ
//   nguyên màu cũ sau khi bấm nút mặt trời. Gán Style thì Setter bên trong nó mới
//   phân giải, và phân giải lại mỗi lần chủ đề đổi.
// =============================================================================
public sealed class StatusDot : Grid
{
    private const double DotSize = 8;
    private const double HaloSize = 16;

    private readonly Ellipse _halo = new() { Width = HaloSize, Height = HaloSize };
    private readonly Ellipse _dot = new() { Width = DotSize, Height = DotSize };

    public StatusDot()
    {
        Width = HaloSize;
        Height = HaloSize;
        VerticalAlignment = VerticalAlignment.Center;
        HorizontalAlignment = HorizontalAlignment.Center;
        _halo.HorizontalAlignment = HorizontalAlignment.Center;
        _halo.VerticalAlignment = VerticalAlignment.Center;
        _dot.HorizontalAlignment = HorizontalAlignment.Center;
        _dot.VerticalAlignment = VerticalAlignment.Center;
        Children.Add(_halo);
        Children.Add(_dot);
        Apply();
    }

    public static readonly DependencyProperty IsLiveProperty = DependencyProperty.Register(
        nameof(IsLive), typeof(bool), typeof(StatusDot),
        new PropertyMetadata(false, (d, _) => ((StatusDot)d).Apply()));

    public bool IsLive
    {
        get => (bool)GetValue(IsLiveProperty);
        set => SetValue(IsLiveProperty, value);
    }

    private void Apply()
    {
        var res = Application.Current.Resources;
        _dot.Style = (Style)res[IsLive ? "StatusDotLiveShape" : "StatusDotIdleShape"];
        _halo.Style = (Style)res["StatusDotHaloShape"];
        _halo.Opacity = IsLive ? 1.0 : 0.0;
        _dot.Opacity = IsLive ? 1.0 : 0.55;
    }
}
