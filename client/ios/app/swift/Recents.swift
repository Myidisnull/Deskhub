// =============================================================================
// Recents.swift — danh sách máy đã kết nối, lưu giữa các lần chạy.
//                 Đối ứng client/macos/app/swift/Recents.swift và Recents.cs bên Windows.
//
// VÌ SAO CÓ Ở ĐÂY MÀ KHÔNG PHẢI Ở CORE
//   Nó là trí nhớ của MỘT CÀI ĐẶT app, không phải một luật của giao thức: hai máy nói
//   chuyện với nhau không cần biết bên kia nhớ gì. Core chỉ nhận những thứ cả sáu
//   client phải nhất trí.
//
// ĐÁNG GIÁ HƠN TRÊN ĐIỆN THOẠI SO VỚI DESKTOP
//   Bản cũ chỉ nhớ ĐÚNG MỘT địa chỉ (khoá "lastAddress"). Ai dùng hai máy — máy ở nhà
//   và máy ở công ty — thì mỗi lần đổi là gõ lại một chuỗi IP trên bàn phím ảo, thao
//   tác khó chịu nhất mà cái app này bắt người dùng làm.
//
// LƯU BẰNG MỘT CHUỖI, KHÔNG PHẢI JSON
//   Tối đa 12 dòng, mỗi dòng ba trường không chứa ký tự phân tách (địa chỉ và loại
//   link là ASCII; tên máy đã lọc). Kéo cả bộ mã hoá JSON vào cho ngần ấy dữ liệu là
//   đổi một phụ thuộc lấy không gì.
// =============================================================================
import Foundation

struct RecentMachine: Identifiable, Hashable, Sendable {
    let address: String
    let name: String
    let link: String

    var id: String { address }
    // Tên rỗng (kết nối bằng tay, host chưa xưng tên) thì địa chỉ chính là tên.
    var displayName: String { name.isEmpty ? address : name }
}

@MainActor
enum Recents {
    private static let key = "recentMachines"
    private static let max = 12
    private static let rowSep: Character = "\n"
    private static let fieldSep: Character = "\t"

    private static var cache: [RecentMachine]?

    static var all: [RecentMachine] {
        if let cache { return cache }
        let loaded = load()
        cache = loaded
        return loaded
    }

    // Ghi nhận một lần kết nối. Địa chỉ trùng thì cập nhật tại chỗ và đẩy lên đầu —
    // danh sách sắp theo lần dùng gần nhất, đúng thứ tự người ta đi tìm.
    static func remember(address: String, name: String = "") {
        let addr = address.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !addr.isEmpty else { return }
        var list = all.filter { $0.address != addr }
        list.insert(RecentMachine(address: addr, name: clean(name), link: guessLink(addr)), at: 0)
        if list.count > max { list.removeSubrange(max...) }
        cache = list
        save(list)
    }

    static func forgetAll() {
        cache = []
        save([])
    }

    // Đoán loại đường từ dải IP. Chỉ để hiện nhãn nên đoán sai không hỏng gì —
    // 100.64.0.0/10 là dải CGNAT mà Tailscale dùng, phần còn lại thì không biết.
    static func guessLink(_ address: String) -> String {
        let host = address.split(separator: ":").first.map(String.init) ?? address
        let octets = host.split(separator: ".")
        guard octets.count == 4, let o1 = Int(octets[0]), let o2 = Int(octets[1]) else { return "" }
        if o1 == 100, (64 ... 127).contains(o2) { return "Tailscale" }
        if o1 == 10 || (o1 == 192 && o2 == 168) || (o1 == 172 && (16 ... 31).contains(o2)) { return "LAN" }
        return ""
    }

    // Bỏ ký tự phân tách khỏi dữ liệu người dùng: một tên máy có tab trong đó sẽ làm
    // lệch mọi trường phía sau khi đọc lại.
    private static func clean(_ text: String) -> String {
        text.replacingOccurrences(of: String(fieldSep), with: " ")
            .replacingOccurrences(of: String(rowSep), with: " ")
    }

    private static func load() -> [RecentMachine] {
        guard let raw = UserDefaults.standard.string(forKey: key), !raw.isEmpty else { return [] }
        return raw.split(separator: rowSep).compactMap { row in
            let fields = row.split(separator: fieldSep, omittingEmptySubsequences: false)
            guard fields.count >= 3 else { return nil }
            return RecentMachine(
                address: String(fields[0]),
                name: String(fields[1]),
                link: String(fields[2])
            )
        }
    }

    private static func save(_ list: [RecentMachine]) {
        let raw = list
            .map { [$0.address, $0.name, $0.link].joined(separator: String(fieldSep)) }
            .joined(separator: String(rowSep))
        UserDefaults.standard.set(raw, forKey: key)
    }
}
