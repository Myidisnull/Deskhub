#include "deskhubp/system/KeepAwake.h"

#include <windows.h>

#include "deskhubp/diag/Log.h"

namespace deskhubp {

namespace {

HANDLE PowerRequestHandle() {
    static HANDLE request = [] {
        static wchar_t reasonText[] = L"Deskhub session active";
        REASON_CONTEXT reason{};
        reason.Version = POWER_REQUEST_CONTEXT_VERSION;
        reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
        reason.Reason.SimpleReasonString = reasonText;
        HANDLE h = PowerCreateRequest(&reason);
        if (!h || h == INVALID_HANDLE_VALUE) {
            LOGW("[KeepAwake] PowerCreateRequest failed: %lu", GetLastError());
            return HANDLE(nullptr);
        }
        return h;
    }();
    return request;
}

}

void SetKeepAwakeActive(bool on) {
    HANDLE request = PowerRequestHandle();
    if (!request) return;
    if (on) {
        PowerSetRequest(request, PowerRequestDisplayRequired);
        PowerSetRequest(request, PowerRequestSystemRequired);
        return;
    }
    PowerClearRequest(request, PowerRequestDisplayRequired);
    PowerClearRequest(request, PowerRequestSystemRequired);
}

}
