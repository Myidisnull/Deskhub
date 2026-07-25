using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Deskhub.Controls;

// =============================================================================
// StatePill — nhãn trạng thái mono viết HOA của một nguồn hoặc một phiên:
// "STREAMING" / "IDLE" / "STARTING…" / "ENDED".
//
// BA TÔNG, KHÔNG PHẢI BA MÀU TUỲ Ý
//   Sống = mint, trung tính = xám trên kính, đứt = đỏ. Đúng ba trạng thái mà sản phẩm
//   này có; thêm tông thứ tư nghĩa là đã bịa ra một trạng thái không tồn tại.
//
// CHỮ MỚI LÀ TÍN HIỆU, MÀU CHỈ LÀ THỨ HAI
//   Nhãn luôn ghi thành CHỮ nên người mù màu vẫn đọc được trạng thái. Vì vậy pill này
//   KHÔNG kèm chấm StatusDot như các chỗ chỉ có màu — chữ đã là kênh dự phòng rồi, và
//   bản thiết kế cũng không vẽ chấm ở đây.
// =============================================================================
public static class StatePill
{
    public enum Tone { Live, Neutral, Danger }

    public static UIElement Build(string label, Tone tone)
    {
        var (border, text) = tone switch
        {
            Tone.Live => ("StatePillLiveBorder", "StatePillLiveText"),
            Tone.Danger => ("StatePillDangerBorder", "StatePillDangerText"),
            _ => ("StatePillNeutralBorder", "StatePillNeutralText"),
        };

        return new Border
        {
            Style = (Style)Application.Current.Resources[border],
            Child = new TextBlock
            {
                // Viết hoa ở đây chứ không ở bảng chữ: bảng chữ giữ chữ thường để dùng
                // lại được ở nút và câu văn (xem "stop" vừa là nhãn pill vừa là nút).
                Text = label.ToUpperInvariant(),
                Style = (Style)Application.Current.Resources[text],
            },
        };
    }
}
