// =============================================================================
// HomeView.swift — chọn vai. Đối ứng client/windows/ui/MainMenuWindow.
//
// Hai việc một màn hình, đúng mô hình AnyDesk (docs/01 §1): "máy này cho người khác
// dùng" và "tôi muốn dùng máy khác". Không đoán ý người dùng, không nhớ lựa chọn
// lần trước — chọn sai vai trên một app điều khiển từ xa là chuyện khó chịu hơn
// nhiều so với bấm thêm một nút.
// =============================================================================
import SwiftUI

struct HomeView: View {
    @Binding var route: Route
    @Bindable var model: AgentModel

    // Danh sách địa chỉ chỉ có khi đang chia sẻ (AgentLoop chụp lúc Start). Ở màn
    // hình này ta chưa share nên chỉ hiện lời nhắc, không hiện số — hiện một địa chỉ
    // mà cổng chưa mở thì người bên kia gõ vào sẽ timeout và tưởng app hỏng.
    var body: some View {
        VStack(spacing: 28) {
            Spacer()

            Image(systemName: "desktopcomputer.and.macbook")
                .font(.system(size: 56))
                .foregroundStyle(.secondary)

            VStack(spacing: 6) {
                Text("Deskhub")
                    .font(.largeTitle.bold())
                Text("Low-latency remote desktop")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }

            HStack(spacing: 16) {
                roleCard(
                    icon: "arrow.up.right.video",
                    title: "Connect",
                    subtitle: "View and control another computer"
                ) {
                    route = .connect
                }

                roleCard(
                    icon: "rectangle.on.rectangle",
                    title: "Share this Mac",
                    subtitle: "Let someone else view and control it"
                ) {
                    model.refreshPermissions()
                    route = .share
                }
            }

            Spacer()
        }
        .padding(40)
        .onAppear { model.refreshPermissions() }
    }

    private func roleCard(
        icon: String,
        title: String,
        subtitle: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            VStack(spacing: 10) {
                Image(systemName: icon)
                    .font(.system(size: 30))
                Text(title)
                    .font(.headline)
                Text(subtitle)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .frame(width: 220, height: 150)
            .padding(12)
            .background(.quaternary.opacity(0.5), in: RoundedRectangle(cornerRadius: 12))
        }
        .buttonStyle(.plain)
    }
}
