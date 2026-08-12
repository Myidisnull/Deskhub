import Foundation

struct BroadcastStatus: Equatable, Sendable {
    static let appGroup = "group.com.ios.deskhub"

    private static let fileName = "broadcast-status.txt"
    private static let sharingKey = "sharing"
    private static let viewersKey = "viewers"
    private static let errorKey = "error"

    var sharing = false
    var viewers = 0
    var error = ""

    static var containerURL: URL? {
        FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: appGroup)
    }

    static func load() -> BroadcastStatus {
        guard let url = fileURL,
              let text = try? String(contentsOf: url, encoding: .utf8)
        else {
            return BroadcastStatus()
        }
        return parse(text)
    }

    static func clear() {
        guard let url = fileURL else { return }
        try? FileManager.default.removeItem(at: url)
    }

    func save() {
        guard let url = BroadcastStatus.fileURL else { return }
        let text = """
        \(BroadcastStatus.sharingKey)=\(sharing ? 1 : 0)
        \(BroadcastStatus.viewersKey)=\(viewers)
        \(BroadcastStatus.errorKey)=\(error)
        """
        try? text.write(to: url, atomically: true, encoding: .utf8)
    }

    private static var fileURL: URL? {
        containerURL?.appendingPathComponent(fileName)
    }

    private static func parse(_ text: String) -> BroadcastStatus {
        var status = BroadcastStatus()
        for line in text.split(separator: "\n") {
            let parts = line.split(separator: "=", maxSplits: 1)
            guard parts.count == 2 else { continue }
            let value = String(parts[1])
            switch String(parts[0]) {
            case sharingKey: status.sharing = value == "1"
            case viewersKey: status.viewers = Int(value) ?? 0
            case errorKey: status.error = value
            default: continue
            }
        }
        return status
    }
}
