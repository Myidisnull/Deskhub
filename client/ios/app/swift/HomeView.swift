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

            ScrollView {
                DevicesPage()
                    .padding()
                    .frame(maxWidth: .infinity, alignment: .leading)
            }
            .tabItem {
                Label(
                    DeskhubClient.string(DHStrSidebarDevices),
                    systemImage: "checkmark.shield"
                )
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
