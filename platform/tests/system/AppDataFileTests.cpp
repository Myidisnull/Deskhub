#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhubp/system/AppDataFile.h"

#include <cstdio>
#include <filesystem>
#include <system_error>

namespace {

constexpr const char* kTestFile = "platform-test-appdata.txt";

void TestRoundTrip() {
    std::printf("[appdata] a small config file writes and reads back intact...\n");
    Check(deskhubp::WriteAppDataFile(kTestFile, "alpha\nbeta\n"), "the write succeeds");
    Check(deskhubp::ReadAppDataFile(kTestFile) == "alpha\nbeta\n",
        "the read returns exactly what was written");

    Check(deskhubp::WriteAppDataFile(kTestFile, "v2"), "a second write succeeds");
    Check(deskhubp::ReadAppDataFile(kTestFile) == "v2", "a rewrite replaces, never appends");

    std::error_code ec;
    std::filesystem::remove(deskhubp::AppDataFilePath(kTestFile), ec);
}

void TestMissingFileReadsAsEmpty() {
    std::printf("[appdata] a missing file is an empty string, not an error...\n");
    Check(deskhubp::ReadAppDataFile("platform-test-never-written.txt").empty(),
        "first launch with no saved state is fine");
}

}

void RunAppDataFileTests() {
    TestRoundTrip();
    TestMissingFileReadsAsEmpty();
}
