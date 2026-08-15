#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/ui/DeviceRows.h"
#include "deskhub/ui/Strings.h"

#include <cstdio>

using namespace deskhub;

namespace {

ui::RecentDevice Recent(const std::string& addr, int64_t lastConnected) {
    ui::RecentDevice device;
    device.addr = addr;
    device.lastConnectedUnix = lastConnected;
    return device;
}

void TestScannedMachinesComeFirst() {
    std::printf("[devicerows] machines answering right now are listed above old ones...\n");
    const std::vector<std::string> scanned = {"192.168.1.10:47777", "192.168.1.11:47777"};
    const std::vector<ui::RecentDevice> recent = {Recent("192.168.1.90:47777", 1000)};

    const std::vector<ui::DeviceRow> rows = ui::BuildDeviceRows(scanned, recent);
    Check(rows.size() == 3, "every machine from both sources gets a row");
    Check(rows[0].origin == ui::DeviceOrigin::OnThisNetwork &&
              rows[1].origin == ui::DeviceOrigin::OnThisNetwork,
        "the two that answered the scan lead the list");
    Check(rows[2].origin == ui::DeviceOrigin::Recent && rows[2].addr == "192.168.1.90:47777",
        "history follows, so a machine you can reach now is never below one you cannot");
}

void TestAMachineInBothPlacesAppearsOnce() {
    std::printf("[devicerows] a remembered machine that is also online is not listed twice...\n");
    const std::vector<std::string> scanned = {"192.168.1.10:47777"};
    const std::vector<ui::RecentDevice> recent = {Recent("192.168.1.10:47777", 1700000000)};

    const std::vector<ui::DeviceRow> rows = ui::BuildDeviceRows(scanned, recent);
    Check(rows.size() == 1, "one machine is one row");
    Check(rows[0].origin == ui::DeviceOrigin::OnThisNetwork,
        "being reachable now is the more useful of the two things to say");
    Check(rows[0].lastConnectedUnix == 1700000000,
        "and it keeps the last-connected time, so merging loses nothing");
}

void TestTheDefaultPortIsNotADifferentMachine() {
    std::printf("[devicerows] a bare address and the same one with the default port match...\n");
    const std::vector<std::string> scanned = {"192.168.1.10:" + std::to_string(kDeskhubPort)};
    const std::vector<ui::RecentDevice> recent = {Recent("192.168.1.10", 900)};

    const std::vector<ui::DeviceRow> rows = ui::BuildDeviceRows(scanned, recent);
    Check(rows.size() == 1, "the stored bare host is the machine the scan just found");
    Check(rows[0].lastConnectedUnix == 900, "so its history is carried onto the scanned row");

    const std::vector<ui::DeviceRow> other =
        ui::BuildDeviceRows({"192.168.1.10:50000"}, {Recent("192.168.1.10:51000", 900)});
    Check(other.size() == 2, "but two explicit, different ports stay two rows");
}

void TestJunkAndDuplicatesAreDropped() {
    std::printf("[devicerows] an empty address never becomes a row...\n");
    const std::vector<ui::DeviceRow> rows =
        ui::BuildDeviceRows({"", "192.168.1.10:47777", "192.168.1.10:47777"}, {Recent("", 5)});
    Check(rows.size() == 1, "blank entries are skipped and a repeat is collapsed");
    Check(ui::BuildDeviceRows({}, {}).empty(), "nothing in, nothing out");
}

void TestEachOriginReadsAsItsOwnWord() {
    std::printf("[devicerows] the Where column says which of the two a row came from...\n");
    Check(std::string(ui::DeviceOriginLabel(ui::DeviceOrigin::OnThisNetwork)) ==
              ui::kDeviceOnThisNetwork,
        "a scanned machine is labelled as being on this network");
    Check(std::string(ui::DeviceOriginLabel(ui::DeviceOrigin::Recent)) == ui::kDeviceRecent,
        "a remembered one is labelled as recent");
    Check(std::string(ui::kDeviceOnThisNetwork) != std::string(ui::kDeviceRecent),
        "the two labels are told apart at a glance");
}

}

void RunDeviceRowsTests() {
    TestScannedMachinesComeFirst();
    TestAMachineInBothPlacesAppearsOnce();
    TestTheDefaultPortIsNotADifferentMachine();
    TestJunkAndDuplicatesAreDropped();
    TestEachOriginReadsAsItsOwnWord();
}
