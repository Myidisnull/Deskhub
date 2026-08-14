#include "deskhubp/system/KeepAwake.h"

#include <IOKit/pwr_mgt/IOPMLib.h>

#include "deskhubp/diag/Log.h"

namespace deskhubp {

namespace {

IOPMAssertionID g_displayAssertion = kIOPMNullAssertionID;
IOPMAssertionID g_idleAssertion = kIOPMNullAssertionID;
IOPMAssertionID g_systemAssertion = kIOPMNullAssertionID;

void CreateAssertion(CFStringRef type, IOPMAssertionID& assertion) {
    if (assertion != kIOPMNullAssertionID) return;
    const IOReturn ret = IOPMAssertionCreateWithName(type, kIOPMAssertionLevelOn,
        CFSTR("Deskhub session active"), &assertion);
    if (ret != kIOReturnSuccess) {
        assertion = kIOPMNullAssertionID;
        LOGW("[KeepAwake] assertion failed: 0x%x", unsigned(ret));
    }
}

void DropAssertion(IOPMAssertionID& assertion) {
    if (assertion == kIOPMNullAssertionID) return;
    IOPMAssertionRelease(assertion);
    assertion = kIOPMNullAssertionID;
}

}

void SetKeepAwakeActive(bool on) {
    if (on) {
        CreateAssertion(kIOPMAssertionTypePreventUserIdleDisplaySleep, g_displayAssertion);
        CreateAssertion(kIOPMAssertionTypePreventUserIdleSystemSleep, g_idleAssertion);
        CreateAssertion(kIOPMAssertionTypePreventSystemSleep, g_systemAssertion);
        return;
    }
    DropAssertion(g_displayAssertion);
    DropAssertion(g_idleAssertion);
    DropAssertion(g_systemAssertion);
}

}
