#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/ui/SecretText.h"

#include <cstdio>
#include <string>

using namespace deskhub;

namespace {

void TestRoundTrip() {
    std::printf("[secret] what is written comes back exactly...\n");
    for (const char* code : {"0000", "0417", "9999", "1234"})
        Check(ui::DecodeSecret(ui::EncodeSecret(code)) == code,
            "every 4-digit code survives encode then decode");
    Check(ui::EncodeSecret("").empty(), "nothing in, nothing out");
    Check(ui::DecodeSecret("").empty(), "an empty field decodes to empty");
}

void TestStoredFormIsNotTheDigits() {
    std::printf("[secret] the stored text does not show the digits...\n");
    const std::string stored = ui::EncodeSecret("0417");
    Check(stored.find("0417") == std::string::npos,
        "the passcode cannot be read straight out of the file");
    Check(stored.front() == ui::kSecretPrefix, "encoded values are marked as encoded");
    Check(ui::EncodeSecret("1111") != ui::EncodeSecret("2222"),
        "different codes encode differently");
    Check(ui::EncodeSecret("1111").find("11") != 0,
        "repeated digits do not repeat in the stored form");
}

void TestLegacyAndCorruptValues() {
    std::printf("[secret] hand-written and corrupt values do not break the file...\n");
    Check(ui::DecodeSecret("0417") == "0417", "a plain value written by hand still works");
    Check(ui::DecodeSecret("@zzzz").empty(), "non-hex after the marker decodes to nothing");
    Check(ui::DecodeSecret("@abc").empty(), "an odd number of hex digits decodes to nothing");
    Check(ui::DecodeSecret("@").empty(), "the marker alone decodes to nothing");
}

}

void RunSecretTextTests() {
    TestRoundTrip();
    TestStoredFormIsNotTheDigits();
    TestLegacyAndCorruptValues();
}
