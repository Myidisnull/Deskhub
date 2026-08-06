import SwiftUI

struct DeviceTable: View {
    let rows: [DeviceListRow]
    let note: String
    let withHistory: Bool
    let enabled: Bool
    let onPick: (DeviceListRow) -> Void

    @State private var selection: DeviceListRow.ID?

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            if withHistory {
                connectable(recentTable)
            } else {
                connectable(scanTable)
            }

            if !note.isEmpty {
                Text(note).font(.caption).foregroundStyle(.secondary)
            }
        }
    }

    private var scanTable: some View {
        Table(rows, selection: $selection) {
            TableColumn("Device") { cell($0, $0.addr) }.width(170)
            TableColumn("Ping") { cell($0, $0.ping) }.width(70)
        }
    }

    private var recentTable: some View {
        Table(rows, selection: $selection) {
            TableColumn("Device") { cell($0, $0.addr) }.width(170)
            TableColumn("Status") { cell($0, $0.status) }.width(100)
            TableColumn("Ping") { cell($0, $0.ping) }.width(70)
            TableColumn("Last connected") { cell($0, $0.lastConnected) }.width(150)
        }
    }

    private func connectable(_ table: some View) -> some View {
        table
            .frame(height: 130)
            .disabled(!enabled)
            .onChange(of: selection) { _, picked in
                guard let picked, let row = rows.first(where: { $0.id == picked })
                else { return }
                selection = nil
                onPick(row)
            }
    }

    private func cell(_ row: DeviceListRow, _ text: String) -> some View {
        Text(text).foregroundStyle(DeviceRowStyle.tint(online: row.online))
    }
}
