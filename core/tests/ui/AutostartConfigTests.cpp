#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/ui/AutostartConfig.h"
#include "deskhub/ui/Brand.h"

#include <cstdio>
#include <string>

using namespace deskhub;

namespace {

bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

void TestDesktopEntryNamesTheBinary() {
    std::printf("[autostart] the XDG entry launches exactly the running binary...\n");
    const std::string entry = ui::BuildXdgAutostartEntry("/opt/deskhub/bin/deskhub");
    Check(Contains(entry, "[Desktop Entry]"), "it is a desktop entry");
    Check(Contains(entry, "Type=Application"), "of type application");
    Check(Contains(entry, "Exec=/opt/deskhub/bin/deskhub\n"),
        "and Exec points at the binary that wrote it");
    Check(Contains(entry, std::string("Name=") + brand::kProductName), "named for the app");
    Check(entry.back() == '\n', "the file ends with a newline");
}

void TestSchtasksArgsElevateAtLogon() {
    std::printf("[autostart] the scheduled task runs at logon with highest rights...\n");
    const std::string create =
        ui::BuildSchtasksCreateArgs("C:\\Program Files\\Deskhub\\Deskhub.exe");
    Check(Contains(create, "/Create"), "it creates a task");
    Check(Contains(create, std::string("/TN \"") + brand::kAutostartTaskId + "\""),
        "with the fixed task name");
    Check(Contains(create, "/SC ONLOGON"), "that fires at logon");
    Check(Contains(create, "/RL HIGHEST"),
        "elevated, because the app manifest requires administrator");
    Check(Contains(create, "\"\\\"C:\\Program Files\\Deskhub\\Deskhub.exe\\\"\""),
        "with the exe path quoted against spaces");
    Check(Contains(create, "/F"), "and replaces a stale task instead of failing");

    Check(Contains(ui::BuildSchtasksQueryArgs(),
              std::string("/Query /TN \"") + brand::kAutostartTaskId + "\""),
        "the query names the same task");
    const std::string del = ui::BuildSchtasksDeleteArgs();
    Check(Contains(del, "/Delete") &&
              Contains(del, std::string("/TN \"") + brand::kAutostartTaskId + "\"") &&
              Contains(del, "/F"),
        "and so does the delete, without a confirmation prompt");
}

}

void RunAutostartConfigTests() {
    TestDesktopEntryNamesTheBinary();
    TestSchtasksArgsElevateAtLogon();
}
