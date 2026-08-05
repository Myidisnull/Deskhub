#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/ui/Strings.h"

#include <cstdio>
#include <string>

using namespace deskhub;

namespace {

bool Contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

void TestEveryLabelSaysSomething() {
    std::printf("[strings] no screen can end up showing an empty label...\n");
    const char* const labels[] = {ui::kAppTitle, ui::kHostIpIntro, ui::kNoNetworkAddress,
        ui::kClientIpPrompt, ui::kPickerTitle, ui::kPickerEachWindow, ui::kShareButton,
        ui::kSharingTitle, ui::kSharingSourcesIntro, ui::kSharingConnectHint, ui::kNothingShared,
        ui::kStopSharing, ui::kShareStartFailed, ui::kQueryingSources, ui::kViewerOpenFailed,
        ui::kConnectionEndedTitle, ui::kDisconnected, ui::kSessionEnded, ui::kSidebarHost,
        ui::kSidebarClient, ui::kSidebarSettings, ui::kHostHeading, ui::kClientHeading,
        ui::kSettingsHeading, ui::kSettingsHint, ui::kRecentDevicesHeading,
        ui::kRecentDevicesHint, ui::kRecentDevicesEmpty, ui::kStatusOnline, ui::kStatusOffline,
        ui::kStatusChecking, ui::kNotSharing, ui::kStartingShare, ui::kPickDisplaysHint,
        ui::kNoDisplayTicked, ui::kStopSelectedDisplay, ui::kDisconnectSelectedViewer,
        ui::kAllowControlLabel, ui::kViewOnlyNote, ui::kRequestControlLabel,
        ui::kPasscodeLabel, ui::kClientPasscodePrompt, ui::kClientPasscodeHint,
        ui::kClientIpPlaceholder, ui::kPasscodeInvalid, ui::kLanDevicesHeading,
        ui::kLanDevicesEmpty, ui::kLanDevicesHint, ui::kScanRescanNote, ui::kScanNoLocalNetwork,
        ui::kConnectPromptTitle, ui::kAppVersion, ui::kProjectUrl, ui::kProjectLinkLabel};
    for (const char* s : labels) Check(s && *s, "every shared UI string is non-empty");
    Check(Contains(ui::PasscodeNote("0417"), "0417"),
        "the sharing status quotes the passcode viewers must enter");
}

void TestConnectingMentionsTheAddress() {
    std::printf("[strings] the connecting line repeats the address the user typed...\n");
    Check(Contains(ui::ConnectingTo("192.168.1.10"), "192.168.1.10"),
        "the user can see which machine is being dialled");
    Check(Contains(ui::ConnectingTo(""), "Connecting to"),
        "an empty address still produces a sentence, not a crash");
}

void TestHostTitleOnlyShowsAKnownSize() {
    std::printf("[strings] the host title shows a size only once one is known...\n");
    Check(ui::HostTitle("192.168.1.10", 0, 0) == "192.168.1.10",
        "no size yet: the title is just the address, with no trailing separator");
    Check(ui::HostTitle("192.168.1.10", 1920, 0) == "192.168.1.10",
        "a half-known size counts as unknown");

    const std::string full = ui::HostTitle("192.168.1.10", 1920, 1080);
    Check(Contains(full, "192.168.1.10"), "the address stays in the title");
    Check(Contains(full, "1920") && Contains(full, "1080"), "both dimensions are shown");
}

void TestThePortIsNeverHardcodedTwice() {
    std::printf("[strings] the port the user is told about is the port we listen on...\n");
    const std::string port = std::to_string(kDeskhubPort);
    Check(Contains(ui::UdpPortLine(), port), "the port line quotes the protocol constant");
    Check(Contains(ui::UdpPortLine(50123), "50123"), "a custom port shows as configured");
    Check(Contains(ui::SharingStatusLine(50123), "50123"),
        "the sharing banner quotes the port actually bound");
    Check(Contains(ui::InvalidAddressHint(), port), "so does the bad-address hint");
    Check(Contains(ui::InvalidAddressHint(), "192.168.1.10"),
        "the hint shows an example, so the user knows what shape to type");
}

void TestSplitHostPortHandlesEveryShape() {
    std::printf("[strings] host:port splitting honours the port and rejects junk...\n");
    std::string host;
    uint16_t port = kDeskhubPort;

    Check(ui::SplitHostPort("192.168.1.10", host, port) && host == "192.168.1.10" &&
              port == kDeskhubPort,
        "a bare host keeps the caller's default port");
    Check(ui::SplitHostPort("192.168.1.10:5000", host, port) && host == "192.168.1.10" &&
              port == 5000,
        "an explicit port is split out");
    Check(ui::SplitHostPort("h:1", host, port) && port == 1, "the lowest port is allowed");
    Check(ui::SplitHostPort("h:65535", host, port) && port == 65535, "so is the highest");

    const char* bad[] = {"", ":5000", "host:", "host:0", "host:65536", "host:1:2", "host:abc"};
    for (const char* s : bad) {
        Check(!ui::SplitHostPort(s, host, port), "malformed input is refused outright");
    }
}

void TestTrimStripsOnlyTheEdges() {
    std::printf("[strings] trimming eats surrounding whitespace and nothing else...\n");
    Check(ui::TrimAscii("  192.168.1.10\r\n") == "192.168.1.10", "edges go");
    Check(ui::TrimAscii("a b") == "a b", "inner spaces stay");
    Check(ui::TrimAscii(" \t\r\n").empty(), "all-whitespace collapses to empty");
    Check(ui::TrimAscii("").empty(), "empty stays empty");
}

void TestParsePositiveUintIsStrict() {
    std::printf("[strings] numeric entry fields fall back instead of guessing...\n");
    Check(ui::ParsePositiveUint("60", 30) == 60, "a plain number parses");
    Check(ui::ParsePositiveUint("0", 30) == 30, "zero is not a usable fps/bitrate");
    Check(ui::ParsePositiveUint("", 30) == 30, "empty falls back");
    Check(ui::ParsePositiveUint("6x", 30) == 30, "trailing junk falls back, not truncates");
    Check(ui::ParsePositiveUint("99999999999", 30) == 30, "overflow falls back");
}

void TestPingLabelQuotesTheMeasurement() {
    std::printf("[strings] the ping label shows the measured number with its unit...\n");
    Check(ui::PingMs(12) == "12 ms", "a normal rtt renders as-is");
    Check(ui::PingMs(0) == "0 ms", "zero still renders a full label");
}

void TestTheAboutLineNamesTheBuild() {
    std::printf("[strings] the sidebar shows which build this is and where it lives...\n");
    Check(Contains(ui::VersionLine(), ui::kAppVersion),
        "the version line quotes the version the build was stamped with");
    Check(std::string(ui::kProjectUrl).rfind("https://github.com/", 0) == 0,
        "the project link points at GitHub over https");
}

void TestScanStatusCountsWhatWasChecked() {
    std::printf("[strings] the scan status says how far it got and on which port...\n");
    const std::string running = ui::ScanningStatus(64, 253, 50123);
    Check(Contains(running, "64") && Contains(running, "253"),
        "the user can see the sweep is progressing");
    Check(Contains(running, "50123"), "and which port is being knocked on");
    Check(Contains(ui::ScanFinishedStatus(1, 253), "1 device"),
        "a single hit is not reported as devices");
    Check(Contains(ui::ScanFinishedStatus(0, 253), "0 devices"),
        "an empty network still reports how much was checked");
}

void TestClampWarningQuotesTheProtocolLimit() {
    std::printf("[strings] the too-many-displays warning quotes the real limit...\n");
    Check(Contains(ui::ShareClampWarning(), std::to_string(kMaxSources)),
        "the number the user reads is the number the protocol enforces");
}

}

void RunStringsTests() {
    TestEveryLabelSaysSomething();
    TestConnectingMentionsTheAddress();
    TestHostTitleOnlyShowsAKnownSize();
    TestThePortIsNeverHardcodedTwice();
    TestSplitHostPortHandlesEveryShape();
    TestTrimStripsOnlyTheEdges();
    TestParsePositiveUintIsStrict();
    TestPingLabelQuotesTheMeasurement();
    TestTheAboutLineNamesTheBuild();
    TestScanStatusCountsWhatWasChecked();
    TestClampWarningQuotesTheProtocolLimit();
}
