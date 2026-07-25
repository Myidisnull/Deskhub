// =============================================================================
// DesignTokens.swift — bảng token của hệ thiết kế Deskhub, dịch 1-1 từ
// _ds/tokens/*.css + theme-light.css của dự án thiết kế.
//
// KHỚP TỪNG CON SỐ VỚI BẢN WINDOWS
//   client/windows/csharp/Themes/Tokens.xaml đã dịch đúng bộ token đó một lần rồi.
//   File này lấy lại CHÍNH những giá trị ấy chứ không dịch lại từ CSS: hai app
//   desktop phải trông giống hệt nhau, và một chênh lệch 1% alpha ở đây sẽ không ai
//   phát hiện ra cho tới lúc đặt hai máy cạnh nhau.
//
// VÌ SAO DÙNG NSColor ĐỘNG CHỨ KHÔNG PHẢI HAI BẢNG MÀU TỰ ĐỔI
//   AppKit đã có sẵn cơ chế đúng: một NSColor dựng bằng closure sẽ tự phân giải lại
//   theo NSAppearance đang vẽ. Đặt colorScheme ở gốc cây view là mọi màu bên dưới tự
//   theo. Tự viết bộ đổi màu thì phải truyền "đang sáng hay tối" qua từng view, và sẽ
//   bỏ sót đúng những chỗ ít ai nhìn — đây cũng là lý do bản WinUI chọn
//   ThemeDictionaries thay vì hai từ điển tự đổi.
//
// NỀN SÁNG ĐẢO CHIỀU LỚP PHỦ, KHÔNG PHẢI ĐẢO ĐỘ ĐẬM CỦA CÙNG MỘT LỚP
//   Ở nền tối mọi bề mặt là một lớp TRẮNG mờ chồng lên nền gần-đen. Cách dịch sai là
//   giữ nguyên ý đó rồi đổi trắng thành đen: thẻ hoá ra một vệt xám mờ trên nền
//   trắng. theme-light.css làm điều khác hẳn — bề mặt trở thành TRẮNG ĐẶC còn nền lùi
//   xuống xám (#eef1f6), tức là thẻ NỔI LÊN khỏi nền chứ không chìm vào nó.
//
// PHÔNG CHỮ — CÓ THAY THẾ
//   Bản thiết kế dùng Space Grotesk (hiển thị) và SF Mono (số). Space Grotesk không
//   có sẵn trên macOS và không kèm được vào bundle mà không thêm phần đóng gói phông;
//   ta dùng đúng thứ chuỗi dự phòng của chính bản thiết kế khai báo ngay sau nó:
//   SF Pro Display cho tiêu đề, SF Mono cho số — cả hai là phông hệ thống, lấy qua
//   Font.system(design:).
//
// LIÊN QUAN: DesignComponents.swift (thành phần dựng từ bảng này),
//            client/windows/csharp/Themes/Tokens.xaml (bản đối ứng)
// =============================================================================
import AppKit
import SwiftUI

// MARK: - Tiện ích dựng màu

private extension NSColor {
    // #RRGGBB + alpha rời. Viết alpha thành số thập phân chứ không nhét vào chuỗi hex
    // như XAML: ở đây không có ràng buộc định dạng nào, và ".045" đọc thẳng ra được
    // giá trị bên CSS còn "0B" thì phải nhẩm.
    convenience init(hex: UInt32, alpha: CGFloat = 1) {
        self.init(
            srgbRed: CGFloat((hex >> 16) & 0xFF) / 255,
            green: CGFloat((hex >> 8) & 0xFF) / 255,
            blue: CGFloat(hex & 0xFF) / 255,
            alpha: alpha
        )
    }
}

// Một màu phân giải lại theo giao diện đang vẽ. `name: nil` là màu động không tên —
// đúng thứ ta cần: nó không vào catalog, chỉ sống trong tiến trình này.
private func dyn(dark: NSColor, light: NSColor) -> Color {
    Color(nsColor: NSColor(name: nil) { appearance in
        appearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua ? dark : light
    })
}

private func dyn(dark: UInt32, light: UInt32, darkAlpha: CGFloat = 1, lightAlpha: CGFloat = 1) -> Color {
    dyn(dark: NSColor(hex: dark, alpha: darkAlpha), light: NSColor(hex: light, alpha: lightAlpha))
}

// MARK: - Màu

enum DS {
    // --- Ink: nền và panel ---
    static let bgCanvas = dyn(dark: 0x05070A, light: 0xDDE3EC)
    static let bgWindow = dyn(dark: 0x07090C, light: 0xEEF1F6)
    static let surfacePanel = dyn(dark: 0x0D1116, light: 0xFFFFFF)
    static let surfaceField = dyn(dark: 0x121820, light: 0xFFFFFF)

    // --- Glass: mọi card/rail/HUD là một lớp trắng MỜ, không phải màu đặc ---
    static let glass = dyn(dark: 0xFFFFFF, light: 0xFFFFFF, darkAlpha: 0.045, lightAlpha: 0.92)
    static let surfaceCard = glass
    static let surfaceCardHover = dyn(dark: 0xFFFFFF, light: 0xFFFFFF, darkAlpha: 0.07, lightAlpha: 1)
    static let surfaceControl = dyn(dark: 0xFFFFFF, light: 0xFFFFFF, darkAlpha: 0.06, lightAlpha: 1)
    static let surfaceControlHover = dyn(dark: 0xFFFFFF, light: 0xF1F4F9, darkAlpha: 0.07, lightAlpha: 1)
    // Không có trong bản thiết kế: nó chỉ định nghĩa tới bậc rê chuột. Bậc nhấn đi
    // tiếp cùng hướng để nút không đứng im lúc bị nhấn.
    static let surfaceControlPress = dyn(dark: 0xFFFFFF, light: 0xE4E9F1, darkAlpha: 0.102, lightAlpha: 1)
    static let borderHairline = dyn(dark: 0xFFFFFF, light: 0x0E1A2A, darkAlpha: 0.09, lightAlpha: 0.16)
    static let bgChrome = dyn(dark: 0x0A0E12, light: 0xFFFFFF, darkAlpha: 0.62, lightAlpha: 0.88)

    // --- Chữ ---
    static let textPrimary = dyn(dark: 0xF2F5F7, light: 0x0A1017)
    static let textSecondary = dyn(dark: 0x7D8892, light: 0x4A5663)
    static let textTertiary = dyn(dark: 0x4D5760, light: 0x6D7885)
    static let textMonoDim = dyn(dark: 0x2F3A44, light: 0x95A0AC)
    static let textOnAccent = dyn(dark: 0x04120C, light: 0xFFFFFF)

    // --- Mint: màu tín hiệu — nhấn, trạng thái sống, tiêu điểm ---
    static let accent = dyn(dark: 0x66E3B8, light: 0x17A97C)
    static let accentHover = dyn(dark: 0x48D3A3, light: 0x12946C)
    static let accentPress = dyn(dark: 0x2BB98A, light: 0x0D7D5B)
    static let accentWash = dyn(dark: 0x66E3B8, light: 0x17A97C, darkAlpha: 0.14, lightAlpha: 0.12)
    static let borderActive = dyn(dark: 0x66E3B8, light: 0x17A97C, darkAlpha: 0.35, lightAlpha: 0.4)
    static let accentDisabled = dyn(dark: 0xFFFFFF, light: 0x0E1A2A, darkAlpha: 0.08, lightAlpha: 0.1)

    // --- Trạng thái ---
    static let statusLive = accent
    static let statusIdle = dyn(dark: 0x7D8892, light: 0x5B6774)
    static let statusWarn = dyn(dark: 0xFFB340, light: 0xB26A00)
    static let statusError = dyn(dark: 0xFF6B61, light: 0xD13A2F)
    static let dangerWash = dyn(dark: 0xFF453A, light: 0xD13A2F, darkAlpha: 0.14, lightAlpha: 0.12)
    static let dangerLine = dyn(dark: 0xFF453A, light: 0xD13A2F, darkAlpha: 0.32, lightAlpha: 0.32)
    static let bannerWarn = dyn(dark: 0xFFB340, light: 0xB26A00, darkAlpha: 0.12, lightAlpha: 0.1)

    // Ô tick khi CHƯA chọn: ở nền tối là một hõm ĐEN (phải tối hơn panel để trông như
    // chỗ lõm xuống chờ được đánh dấu); ở nền sáng, một hõm tối sẽ đọc thành ĐÃ tick,
    // nên nó thành ô trắng và dựa vào viền tóc để vẫn thấy được.
    static let checkboxOff = dyn(dark: NSColor(hex: 0x000000, alpha: 0.35), light: NSColor(hex: 0xFFFFFF))

    // --- Nguồn sáng: một cái mỗi màn, không bao giờ là nền ---
    static let glowCobalt = dyn(dark: 0x1B3FA0, light: 0x1B3FA0, darkAlpha: 0.55, lightAlpha: 0.18)
    // Quầng mint sau thứ đang được rê chuột (glow-accent).
    static let glowAccent = dyn(dark: 0x66E3B8, light: 0x17A97C, darkAlpha: 0.14, lightAlpha: 0.16)
    static let surfaceThumb = dyn(dark: 0x16233D, light: 0xDBE4F2)

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
    static let radius2Xl: CGFloat = 22 // vỏ cửa sổ
    static let radiusPill: CGFloat = 999
}

// MARK: - Kích thước bố cục (tokens/spacing.css)

extension DS {
    static let railWidth: CGFloat = 74
    static let railItem: CGFloat = 42
    static let controlHeightSm: CGFloat = 30
    static let controlHeight: CGFloat = 38
    static let controlHeightLg: CGFloat = 44
    static let fieldHeight: CGFloat = 66 // ô địa chỉ hero
    static let screenGutter: CGFloat = 56
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
    // Thang chữ. Tên giữ nguyên tên biến CSS để tra chéo được.
    static let textHero: CGFloat = 62
    static let textDisplay: CGFloat = 46
    static let textTitle: CGFloat = 32
    static let textSubtitle: CGFloat = 24
    static let textStat: CGFloat = 34
    static let textBodyLg: CGFloat = 15
    static let textBody: CGFloat = 14
    static let textBodySm: CGFloat = 13
    static let textCaption: CGFloat = 12
    static let textMicro: CGFloat = 11
    static let textMono: CGFloat = 14
    static let textMonoSm: CGFloat = 11
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
    // tracking-display = -.035em, tracking-title = -.03em, tracking-label = .2em.
    static func trackDisplay(_ size: CGFloat) -> CGFloat { -0.035 * size }
    static func trackTitle(_ size: CGFloat) -> CGFloat { -0.03 * size }
    static func trackLabel(_ size: CGFloat) -> CGFloat { 0.2 * size }
}
