import ReplayKit
import SwiftUI

struct BroadcastPicker: UIViewRepresentable {
    static let preferredSize = CGSize(width: 60, height: 60)

    let extensionBundleId: String

    func makeUIView(context _: Context) -> RPSystemBroadcastPickerView {
        let picker = RPSystemBroadcastPickerView(
            frame: CGRect(origin: .zero, size: BroadcastPicker.preferredSize)
        )
        picker.preferredExtension = extensionBundleId
        picker.showsMicrophoneButton = false
        return picker
    }

    func updateUIView(_: RPSystemBroadcastPickerView, context _: Context) {}
}
