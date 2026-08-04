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
        ui::kConnectionEndedTitle, ui::kDisconnected, ui::kSessionEnded};
    for (const char* s : labels) Check(s && *s, "every shared UI string is non-empty");
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
    Check(Contains(ui::InvalidAddressHint(), port), "so does the bad-address hint");
    Check(Contains(ui::InvalidAddressHint(), "192.168.1.10"),
        "the hint shows an example, so the user knows what shape to type");
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
    TestTrimStripsOnlyTheEdges();
    TestParsePositiveUintIsStrict();
    TestClampWarningQuotesTheProtocolLimit();
}
