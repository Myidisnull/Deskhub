import Foundation

struct LocalAddress: Identifiable, Hashable, Sendable {
    let ip: String
    let name: String

    var id: String { ip + name }

    static func all() -> [LocalAddress] {
        String(cString: dh_local_addresses())
            .split(separator: "\n")
            .compactMap { line in
                let parts = line.split(separator: "\t", maxSplits: 1)
                guard let ip = parts.first else { return nil }
                return LocalAddress(
                    ip: String(ip), name: parts.count > 1 ? String(parts[1]) : ""
                )
            }
    }
}
