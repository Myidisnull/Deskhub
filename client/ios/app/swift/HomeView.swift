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
    }
}
