import SwiftUI

struct PaletteRgb {
    let red: Double
    let green: Double
    let blue: Double
}

#if os(macOS)
    import AppKit

    func adaptiveColor(light: PaletteRgb, dark: PaletteRgb) -> Color {
        Color(nsColor: NSColor(name: nil) { appearance in
            let rgb = appearance.bestMatch(from: [.aqua, .darkAqua]) == .darkAqua ? dark : light
            return NSColor(srgbRed: rgb.red, green: rgb.green, blue: rgb.blue, alpha: 1)
        })
    }
#else
    import UIKit

    func adaptiveColor(light: PaletteRgb, dark: PaletteRgb) -> Color {
        Color(uiColor: UIColor { traits in
            let rgb = traits.userInterfaceStyle == .dark ? dark : light
            return UIColor(red: rgb.red, green: rgb.green, blue: rgb.blue, alpha: 1)
        })
    }
#endif

enum DeskhubPalette {
    static let sidebar = Color(red: 0.122, green: 0.161, blue: 0.216)
    static let accent = Color(red: 0.145, green: 0.388, blue: 0.922)
    static let navText = Color(red: 0.820, green: 0.835, blue: 0.859)
    static let footnote = Color(red: 0.580, green: 0.639, blue: 0.722)

    static let heading = adaptiveColor(
        light: PaletteRgb(red: 0.067, green: 0.094, blue: 0.153),
        dark: PaletteRgb(red: 0.898, green: 0.906, blue: 0.922)
    )
    static let muted = adaptiveColor(
        light: PaletteRgb(red: 0.420, green: 0.447, blue: 0.502),
        dark: PaletteRgb(red: 0.612, green: 0.639, blue: 0.686)
    )
    static let online = adaptiveColor(
        light: PaletteRgb(red: 0.0, green: 0.569, blue: 0.235),
        dark: PaletteRgb(red: 0.290, green: 0.871, blue: 0.502)
    )
    static let offline = adaptiveColor(
        light: PaletteRgb(red: 0.784, green: 0.157, blue: 0.157),
        dark: PaletteRgb(red: 0.973, green: 0.443, blue: 0.443)
    )
}

enum DeviceRowStyle {
    static func tint(online: Bool?) -> Color {
        switch online {
        case true: return DeskhubPalette.online
        case false: return DeskhubPalette.offline
        default: return DeskhubPalette.heading
        }
    }
}

struct DeviceListView: View {
    let heading: String
    let note: String
    let rows: [DeviceListRow]
    let enabled: Bool
    let onPick: (DeviceListRow) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            if !heading.isEmpty {
                Text(heading).font(.headline)
            }

            ForEach(rows) { row in
                Button {
                    onPick(row)
                } label: {
                    HStack(spacing: 12) {
                        VStack(alignment: .leading, spacing: 1) {
                            Text(row.addr).foregroundStyle(DeviceRowStyle.tint(online: row.online))
                            if !row.detail.isEmpty {
                                Text(row.detail).font(.caption).foregroundStyle(.secondary)
                            }
                        }
                        Spacer(minLength: 0)
                        Text(row.ping)
                            .font(.caption)
                            .foregroundStyle(DeviceRowStyle.tint(online: row.online))
                    }
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .disabled(!enabled)
            }

            if !note.isEmpty {
                Text(note).font(.caption).foregroundStyle(.secondary)
            }
        }
    }
}

struct DeviceListRow: Identifiable, Hashable {
    let addr: String
    let passcode: String
    let ping: String
    let detail: String
    let online: Bool?
    var id: String { addr }

    init(_ hit: ScanHit, passcode: String) {
        addr = hit.addr
        self.passcode = passcode
        ping = hit.ping
        detail = ""
        online = nil
    }

    init(_ device: RecentDevice) {
        addr = device.addr
        passcode = device.passcode
        ping = device.ping
        detail = "\(device.status)  \(device.lastConnected)"
        online = device.online
    }
}
