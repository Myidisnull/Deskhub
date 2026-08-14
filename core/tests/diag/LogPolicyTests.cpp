#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/diag/LogPolicy.h"

#include <cstdio>
#include <string>

using namespace deskhub;

namespace {

void TestClampRaisesTinyFilesAndCapsHugeOnes() {
    std::printf("[log-policy] file-size bounds stay within the supported window...\n");
    diag::LogPolicy tiny;
    tiny.maxFileMb = 0;
    Check(diag::ClampLogPolicy(tiny).maxFileMb == diag::kMinLogMaxFileMb,
        "below the floor is raised");

    diag::LogPolicy huge;
    huge.maxFileMb = 100000;
    Check(diag::ClampLogPolicy(huge).maxFileMb == diag::kMaxLogMaxFileMb,
        "above the ceiling is capped");
}

void TestDeleteCannotBeatCompress() {
    std::printf("[log-policy] delete retention cannot be shorter than compress...\n");
    diag::LogPolicy p;
    p.compressAfterDays = 10;
    p.deleteAfterDays = 3;
    const diag::LogPolicy clamped = diag::ClampLogPolicy(p);
    Check(clamped.deleteAfterDays == 10, "delete is lifted to match compress");
}

void TestLogNamesAreRecognised() {
    std::printf("[log-policy] only System Runtime log names are accepted...\n");
    Check(diag::IsDeskhubLogName("system-runtime-20260101.log"), "the daily active name matches");
    Check(diag::IsDeskhubLogName("system-runtime-20260101-120000-1234.log"),
        "a live log name matches");
    Check(diag::IsDeskhubGzipLogName("system-runtime-20260101-120000-1234.log.gz"),
        "a compressed log name matches");
    Check(!diag::IsDeskhubLogName("system-runtime-20260101-120000-1234.log.gz"),
        "compressed files are not treated as live logs");
    Check(!diag::IsDeskhubLogName("ui-settings.txt"), "settings files are ignored");
    Check(!diag::IsDeskhubGzipLogName("notes.gz"), "unrelated gzips are ignored");
}

void TestMaxBytesMatchesMb() {
    std::printf("[log-policy] the byte ceiling is megabytes times 1024 squared...\n");
    diag::LogPolicy p;
    p.maxFileMb = 2;
    Check(diag::LogMaxFileBytes(p) == 2ull * 1024ull * 1024ull, "2 MB is exact");
}

void TestLogDirShape() {
    std::printf("[log-policy] log directory strings reject control characters...\n");
    Check(diag::IsPlausibleLogDir(""), "empty means the default folder");
    Check(diag::IsPlausibleLogDir("/var/log/deskhub"), "a normal absolute path is fine");
    Check(!diag::IsPlausibleLogDir("bad\npath"), "newlines are rejected");
    Check(!diag::IsPlausibleLogDir(std::string(diag::kMaxLogDirChars + 1, 'a')),
        "overlong paths are rejected");
}

}

void RunLogPolicyTests() {
    TestClampRaisesTinyFilesAndCapsHugeOnes();
    TestDeleteCannotBeatCompress();
    TestLogNamesAreRecognised();
    TestMaxBytesMatchesMb();
    TestLogDirShape();
}
