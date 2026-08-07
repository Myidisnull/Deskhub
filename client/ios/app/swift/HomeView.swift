import SwiftUI

struct HomeView: View {
    @Bindable var model: SessionModel

    var body: some View {
        TabView {
            ConnectView(model: model)
                .tabItem {
                    Label(DeskhubClient.string(DHStrSidebarClient), systemImage: "display")
                }

            SettingsView(settings: model.settings) { port in
                model.discovery.usePort(port)
            }
            .tabItem {
                Label(DeskhubClient.string(DHStrSidebarSettings), systemImage: "gearshape")
            }
        }
    }
}
