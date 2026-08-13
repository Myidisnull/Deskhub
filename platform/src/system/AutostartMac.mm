#include "deskhubp/system/Autostart.h"

#import <ServiceManagement/ServiceManagement.h>

#include "deskhubp/diag/Log.h"

namespace deskhubp {

bool SetAutostartEnabled(bool on) {
    if (@available(macOS 13.0, *)) {
        SMAppService* service = [SMAppService mainAppService];
        NSError* error = nil;
        const bool ok = on ? [service registerAndReturnError:&error]
                           : [service unregisterAndReturnError:&error];
        if (!ok && error)
            LOGW("[Autostart] %s failed: %s", on ? "register" : "unregister",
                error.localizedDescription.UTF8String);
        if (!on && !ok && error.code == kSMErrorJobNotFound) return true;
        return ok;
    }
    return false;
}

bool AutostartEnabled() {
    if (@available(macOS 13.0, *)) {
        return [SMAppService mainAppService].status == SMAppServiceStatusEnabled;
    }
    return false;
}

}
