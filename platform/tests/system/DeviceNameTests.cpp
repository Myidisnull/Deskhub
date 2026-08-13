#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/ui/UiSettings.h"
#include "deskhubp/system/DeviceName.h"
#include "deskhubp/system/UiSettingsStore.h"

#include <cstdio>
#include <string>

void RunDeviceNameTests() {
    std::printf("[devname] the machine offers a usable default device name...\n");

    const std::string name = deskhubp::LocalDeviceName();
    Check(name == deskhubp::LocalDeviceName(), "the name is stable across calls");
    Check(deskhub::ui::TruncateDeviceName(name) == name,
        "it is already within the device-name limits");
    Check(!name.empty(), "this machine can name itself");

    Check(!deskhubp::SessionDeviceName().empty(),
        "a session always has a name to announce, even with no name saved");
}
