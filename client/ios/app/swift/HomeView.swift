import SwiftUI

struct HomeView: View {
    @Bindable var model: SessionModel

    var body: some View {
        TabView {
            ConnectView(model: model)
                .tabItem {
                    Label(DeskhubClient.string(DHStrSidebarClient), systemImage: "display")
                }

            SharingView(model: model.sharing)
                .tabItem {
                    Label(
                        DeskhubClient.string(DHStrSidebarHost),
                        systemImage: "rectangle.on.rectangle"
                    )
                }

            SettingsView(settings: model.settings) { port in
                model.discovery.usePort(port)
            }
            .tabItem {
                Label(DeskhubClient.string(DHStrSidebarSettings), systemImage: "gearshape")
            }
        }
        .task { await model.sharing.poll() }
        .task { await FilesHost.shared.run() }
        .onChange(of: model.sharing.status.sharing) { _, sharing in
            if sharing { FilesHost.shared.stop() }
        }
        .onChange(of: model.settings.acceptFiles) { _, _ in
            if !model.settings.acceptFiles { FilesHost.shared.stop() }
        }
    }
}
