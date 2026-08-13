#include "deskhubp/system/DeviceName.h"

#include "deskhub/ui/UiSettings.h"

#include <unistd.h>

#include <cstdlib>

namespace deskhubp {

std::string LocalDeviceName() {
    char host[256] = {};
    if (gethostname(host, sizeof(host) - 1) == 0 && host[0]) {
        std::string name(host);
        const size_t dot = name.find('.');
        if (dot != std::string::npos) name.resize(dot);
        if (!name.empty() && name != "localhost") return deskhub::ui::TruncateDeviceName(name);
    }

    const char* user = std::getenv("USER");
    if (!user || !*user) user = std::getenv("LOGNAME");
    if (!user || !*user) return {};
    return deskhub::ui::TruncateDeviceName(user);
}

}
