// =============================================================================
// ConnectView.swift — ô nhập IP + nút Connect (vai CLIENT).
//
// GIAO DIỆN TRẦN (2026-07-27)
//   SwiftUI dựng sẵn, không hệ thiết kế riêng, không chữ hướng dẫn, không danh sách
//   máy gần đây, không ô "chỉ xem". Màn này còn đúng hai thứ: một ô nhập và một nút —
//   giống hệt bản Android.
//
//   Cổng 47777 do tầng C++ điền (ParseNetAddr chỉ nhận IP trần và từ chối chuỗi có
//   ':'), Swift không lặp lại hằng số đó để hai nơi không lệch nhau.
// =============================================================================
import SwiftUI

struct ConnectView: View {
    @Bindable var model: SessionModel

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            TextField("Host IP address", text: $model.address)
                .textFieldStyle(.roundedBorder)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .keyboardType(.numbersAndPunctuation)
                .submitLabel(.go)
                .onSubmit(model.connect)
                .disabled(model.isConnecting)

            HStack(spacing: 12) {
                Button("Connect", action: model.connect)
                    .buttonStyle(.borderedProminent)
                    .disabled(model.address.isEmpty || model.isConnecting)

                if model.isConnecting {
                    ProgressView()
                }
            }

            if !model.connectError.isEmpty {
                Text(model.connectError)
                    .foregroundStyle(.red)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Spacer()
        }
        .padding()
    }
}
