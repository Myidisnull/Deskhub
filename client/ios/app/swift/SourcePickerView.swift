import SwiftUI

struct SourcePickerView: View {
    @Bindable var model: SessionModel
    let sources: [Source]

    @State private var picked: UInt8?

    private var selectedId: UInt8 { picked ?? sources.first?.id ?? 0 }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(model.address)
                .font(.headline)

            ForEach(sources) { source in
                Button {
                    picked = source.id
                } label: {
                    HStack(spacing: 12) {
                        Image(systemName: source.id == selectedId
                            ? "largecircle.fill.circle"
                            : "circle")
                        VStack(alignment: .leading) {
                            Text(source.displayName)
                            Text(source.sizeLabel)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        Spacer()
                    }
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }

            Spacer()

            Button("Start viewing") { model.startStream(sourceId: selectedId) }
                .buttonStyle(.borderedProminent)
                .frame(maxWidth: .infinity)
                .disabled(sources.isEmpty)
        }
        .padding()
    }
}
