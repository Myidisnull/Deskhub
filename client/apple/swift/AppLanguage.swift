import Foundation

enum AppLanguage: String, CaseIterable, Identifiable {
    case system = ""
    case en = "en"
    case zhHans = "zh-Hans"
    case fr = "fr"
    case de = "de"
    case ru = "ru"
    case ja = "ja"
    case ko = "ko"
    case ar = "ar"

    var id: String { rawValue }

    var label: String {
        switch self {
        case .system: DeskhubClient.string(DHStrLanguageSystem)
        case .en: "English"
        case .zhHans: "简体中文"
        case .fr: "Français"
        case .de: "Deutsch"
        case .ru: "Русский"
        case .ja: "日本語"
        case .ko: "한국어"
        case .ar: "العربية"
        }
    }

    static func fromStored(_ code: String) -> AppLanguage {
        AppLanguage(rawValue: code) ?? .system
    }
}
