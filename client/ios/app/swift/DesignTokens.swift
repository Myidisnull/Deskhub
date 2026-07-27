// =============================================================================
// DesignTokens.swift — bảng token của hệ thiết kế Deskhub cho iOS.
//
// CÙNG MỘT BỘ SỐ VỚI macOS VÀ WINDOWS
//   client/macos/app/swift/DesignTokens.swift
//   Tokens.xaml đã dịch bộ token này từ _ds/tokens/*.css. File này lấy lại CHÍNH
//   những giá trị màu ấy chứ không dịch lại lần nữa: ba nền tảng phải trông cùng một
//   sản phẩm, và một chênh lệch 1% alpha ở đây sẽ không ai phát hiện ra cho tới lúc
//   đặt điện thoại cạnh màn hình máy tính.
//
// KÍCH THƯỚC THÌ KHÔNG CHÉP — ĐÓ LÀ CHỖ DUY NHẤT ĐƯỢC LỆCH
//   Màu là bản sắc, kích thước là công thái học. Ngón tay không phải con trỏ chuột:
//   Apple đòi vùng chạm tối thiểu 44pt, còn desktop cho nút cao 38 và gutter 56 vì
//   ở đó có chuột nhắm chính xác tới từng pixel. Chép nguyên số đo desktop sang màn
//   4.7 inch sẽ ra một app đúng màu nhưng bấm trượt liên tục — nên mọi hằng số bố
//   cục dưới đây đều đã chỉnh cho tay người, và mỗi chỗ lệch đều ghi rõ lệch vì sao.
//
// VÌ SAO UIColor ĐỘNG CHỨ KHÔNG PHẢI HAI BẢNG MÀU TỰ ĐỔI
//   UIKit đã có sẵn cơ chế đúng: UIColor dựng bằng closure tự phân giải lại theo
//   traitCollection đang vẽ. ContentView đặt .preferredColorScheme ở gốc cây view là
//   mọi màu bên dưới tự theo. Tự viết bộ đổi màu thì phải truyền "đang sáng hay tối"
//   qua từng view, và sẽ bỏ sót đúng những chỗ ít ai nhìn.
//
// NỀN SÁNG ĐẢO CHIỀU LỚP PHỦ, KHÔNG PHẢI ĐẢO ĐỘ ĐẬM CỦA CÙNG MỘT LỚP
//   Ở nền tối mọi bề mặt là một lớp TRẮNG mờ chồng lên nền gần-đen. Ở nền sáng, bề
//   mặt trở thành TRẮNG ĐẶC còn nền lùi xuống xám (#eef1f6) — thẻ NỔI LÊN khỏi nền
//   chứ không chìm vào nó.
//
// LIÊN QUAN: DesignButtons/Controls/Layout/Rows/Surfaces/Text.swift (dựng từ bảng
//            này), client/macos/app/swift/DesignTokens.swift (bản đối ứng)
// =============================================================================
import SwiftUI
import UIKit

// MARK: - Tiện ích dựng màu

private extension UIColor {
    // #RRGGBB + alpha rời. Viết alpha thành số thập phân chứ không nhét vào chuỗi hex:
    // ".045" đọc thẳng ra được giá trị bên CSS còn "0B" thì phải nhẩm.
    convenience init(hex: UInt32, alpha: CGFloat = 1) {
        self.init(
            red: CGFloat((hex >> 16) & 0xFF) / 255,
            green: CGFloat((hex >> 8) & 0xFF) / 255,
            blue: CGFloat(hex & 0xFF) / 255,
            alpha: alpha
        )
    }
}

// Một màu phân giải lại theo giao diện đang vẽ.
private func dyn(dark: UIColor, light: UIColor) -> Color {
    Color(uiColor: UIColor { traits in
        traits.userInterfaceStyle == .dark ? dark : light
    })
}

private func dyn(dark: UInt32, light: UInt32, darkAlpha: CGFloat = 1, lightAlpha: CGFloat = 1) -> Color {
    dyn(dark: UIColor(hex: dark, alpha: darkAlpha), light: UIColor(hex: light, alpha: lightAlpha))
}

// MARK: - Màu

enum DS {
    // --- Ink: nền và panel ---
    static let bgCanvas = dyn(dark: 0x05070A, light: 0xDDE3EC)
    static let bgWindow = dyn(dark: 0x07090C, light: 0xEEF1F6)
    static let surfacePanel = dyn(dark: 0x0D1116, light: 0xFFFFFF)
    static let surfaceField = dyn(dark: 0x121820, light: 0xFFFFFF)

    // --- Glass: mọi card/HUD là một lớp trắng MỜ, không phải màu đặc ---
    static let glass = dyn(dark: 0xFFFFFF, light: 0xFFFFFF, darkAlpha: 0.045, lightAlpha: 0.92)
    static let surfaceCard = glass
    static let surfaceControl = dyn(dark: 0xFFFFFF, light: 0xFFFFFF, darkAlpha: 0.06, lightAlpha: 1)
    // Desktop có ba bậc: nghỉ → rê chuột → nhấn. Cảm ứng KHÔNG CÓ bậc giữa (không có
    // con trỏ để mà rê), nên bậc "rê" biến mất và bậc nhấn đi thẳng tới đích: một nút
    // đổi sang màu hover mờ nhạt khi chạm sẽ không kịp thấy trong 100ms ngón tay đè.
    static let surfaceControlPress = dyn(dark: 0xFFFFFF, light: 0xE4E9F1, darkAlpha: 0.102, lightAlpha: 1)
    static let surfaceCardPress = dyn(dark: 0xFFFFFF, light: 0xF1F4F9, darkAlpha: 0.09, lightAlpha: 1)
    static let borderHairline = dyn(dark: 0xFFFFFF, light: 0x0E1A2A, darkAlpha: 0.09, lightAlpha: 0.16)
    static let bgChrome = dyn(dark: 0x0A0E12, light: 0xFFFFFF, darkAlpha: 0.62, lightAlpha: 0.88)

    // --- Chữ ---
    static let textPrimary = dyn(dark: 0xF2F5F7, light: 0x0A1017)
    static let textSecondary = dyn(dark: 0x7D8892, light: 0x4A5663)
    static let textTertiary = dyn(dark: 0x4D5760, light: 0x6D7885)
    static let textOnAccent = dyn(dark: 0x04120C, light: 0xFFFFFF)

    // --- Mint: màu tín hiệu — nhấn, trạng thái sống, tiêu điểm ---
    static let accent = dyn(dark: 0x66E3B8, light: 0x17A97C)
    static let accentPress = dyn(dark: 0x2BB98A, light: 0x0D7D5B)
    static let accentWash = dyn(dark: 0x66E3B8, light: 0x17A97C, darkAlpha: 0.14, lightAlpha: 0.12)
    static let borderActive = dyn(dark: 0x66E3B8, light: 0x17A97C, darkAlpha: 0.35, lightAlpha: 0.4)
    static let accentDisabled = dyn(dark: 0xFFFFFF, light: 0x0E1A2A, darkAlpha: 0.08, lightAlpha: 0.1)

    // --- Trạng thái ---
    static let statusLive = accent
    static let statusIdle = dyn(dark: 0x7D8892, light: 0x5B6774)
    static let statusError = dyn(dark: 0xFF6B61, light: 0xD13A2F)
    static let dangerWash = dyn(dark: 0xFF453A, light: 0xD13A2F, darkAlpha: 0.14, lightAlpha: 0.12)
    static let dangerLine = dyn(dark: 0xFF453A, light: 0xD13A2F, darkAlpha: 0.32, lightAlpha: 0.32)

    // Ô tick khi CHƯA chọn: ở nền tối là một hõm ĐEN (phải tối hơn panel để trông như
    // chỗ lõm xuống chờ được đánh dấu); ở nền sáng, một hõm tối sẽ đọc thành ĐÃ tick,
    // nên nó thành ô trắng và dựa vào viền tóc để vẫn thấy được.
    static let checkboxOff = dyn(dark: UIColor(hex: 0x000000, alpha: 0.35), light: UIColor(hex: 0xFFFFFF))

    // --- Nguồn sáng: một cái mỗi màn, không bao giờ là nền ---
    static let glowCobalt = dyn(dark: 0x1B3FA0, light: 0x1B3FA0, darkAlpha: 0.55, lightAlpha: 0.18)
    static let glowAccent = dyn(dark: 0x66E3B8, light: 0x17A97C, darkAlpha: 0.14, lightAlpha: 0.16)

    // Khung video LUÔN đen, kể cả ở giao diện sáng: vùng letterbox quanh khung hình
    // phải là màu KHÔNG CÓ, chứ không phải một màu nhạt — nếu không, mắt sẽ đọc nó
    // thành một phần của hình.
    static let bgVideo = Color.black
}

// MARK: - Bo góc (tokens/radius.css) — không gì sắc, không gì tròn hẳn trừ pill

extension DS {
    static let radiusSm: CGFloat = 10 // chip, control nhỏ
    static let radiusMd: CGFloat = 12 // nút, thumbnail
    static let radiusLg: CGFloat = 16 // field, thẻ danh sách
    static let radiusXl: CGFloat = 18 // panel, tile
    static let radiusPill: CGFloat = 999
}

// MARK: - Kích thước bố cục — CHỖ DUY NHẤT LỆCH KHỎI BẢN DESKTOP

extension DS {
    // 44pt là vùng chạm tối thiểu của Human Interface Guidelines. Bản desktop để
    // controlHeight 38 vì chuột nhắm chính xác; ngón tay thì không, nên bậc md dâng
    // lên đúng ngưỡng 44 và bậc sm — chỉ dùng cho phím tắt trên màn xem, nơi nút nào
    // cũng nằm trong tầm ngón cái — giữ 34 để một hàng phím vừa bề ngang điện thoại.
    static let controlHeightSm: CGFloat = 34
    static let controlHeight: CGFloat = 44
    static let controlHeightLg: CGFloat = 52
    static let iconButton: CGFloat = 44

    // Ô địa chỉ hero: desktop 66. Trên điện thoại nó chiếm gần trọn bề ngang nên
    // 58 đã đủ "to hơn mọi thứ khác", mà vẫn chừa chỗ cho bàn phím ảo bên dưới.
    static let fieldHeight: CGFloat = 58

    // Gutter 20 thay vì 56: trên màn 390pt, gutter desktop ăn 29% bề ngang màn hình.
    static let screenGutter: CGFloat = 20
    static let hairline: CGFloat = 1
}

// MARK: - Chuyển động (tokens/motion.css)

extension DS {
    // Nhanh, im lặng, chỉ đổi màu/viền/độ mờ. Không gì phóng to, không gì nảy.
    static let easeStandard = Animation.timingCurve(0.4, 0, 0.2, 1, duration: 0.14)
    static let easeFast = Animation.timingCurve(0.4, 0, 0.2, 1, duration: 0.22)
}

// MARK: - Chữ (tokens/typography.css)

extension DS {
    // Thang chữ. Tên giữ nguyên tên biến CSS để tra chéo được với bản desktop; hai
    // bậc trên cùng (hero 62 / display 46) KHÔNG có ở đây vì không màn nào của bản
    // mobile dùng tới — một dòng 46pt trên màn hẹp là hai, ba dòng gãy.
    static let textTitle: CGFloat = 28 // desktop 32
    static let textSubtitle: CGFloat = 20 // desktop 24
    static let textStat: CGFloat = 28 // desktop 34
    static let textBodyLg: CGFloat = 16 // desktop 15 — cỡ chữ đọc chuẩn của iOS
    static let textBody: CGFloat = 15
    static let textBodySm: CGFloat = 13
    static let textCaption: CGFloat = 12
    static let textMono: CGFloat = 14
    static let textMonoSm: CGFloat = 12 // desktop 11: mono 11pt trên màn retina nhỏ quá
    static let textLabel: CGFloat = 10 // nhãn eyebrow viết hoa

    // Phông hiển thị — tiêu đề màn và mọi con số to. SF Pro Display, đúng thứ bản
    // thiết kế đặt ngay sau Space Grotesk trong chuỗi dự phòng của nó.
    static func display(_ size: CGFloat, weight: Font.Weight = .bold) -> Font {
        .system(size: size, weight: weight, design: .default)
    }

    // Phông giao diện — chữ thường trong UI.
    static func ui(_ size: CGFloat, weight: Font.Weight = .regular) -> Font {
        .system(size: size, weight: weight)
    }

    // Mono — MỌI con số sản phẩm hiện ra đều dùng kiểu này.
    static func mono(_ size: CGFloat, weight: Font.Weight = .regular) -> Font {
        .system(size: size, weight: weight, design: .monospaced)
    }

    // Giãn chữ tính bằng em ở CSS, bằng POINT ở SwiftUI (.tracking) — nhân với cỡ chữ.
    static func trackDisplay(_ size: CGFloat) -> CGFloat { -0.035 * size }
    static func trackTitle(_ size: CGFloat) -> CGFloat { -0.03 * size }
    static func trackLabel(_ size: CGFloat) -> CGFloat { 0.2 * size }
}
