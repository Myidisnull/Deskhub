#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/session/ShareArgs.h"
#include "deskhub/session/ShareFlow.h"

#include <cstdio>
#include <vector>

using namespace deskhub;

namespace {

std::vector<wchar_t*> SplitArgs(std::wstring& line, std::vector<std::wstring>& storage) {
    storage.clear();
    storage.push_back(L"deskhub.exe");
    size_t pos = 0;
    while (pos < line.size()) {
        while (pos < line.size() && line[pos] == L' ') ++pos;
        const size_t end = line.find(L' ', pos);
        if (pos >= line.size()) break;
        storage.push_back(line.substr(pos, end - pos));
        if (end == std::wstring::npos) break;
        pos = end;
    }
    std::vector<wchar_t*> argv;
    for (auto& s : storage) argv.push_back(s.data());
    return argv;
}

void TestArgsRoundTrip() {
    std::printf("[share] the elevated relaunch carries sources and options unchanged...\n");
    media::ShareSource src;
    src.targetId = 0x1abcd;
    src.name = "Display 1 (main)";
    media::AgentOptions opt;
    opt.fps = 24;
    opt.bitrateMbps = 12;
    opt.maxDim = 2560;

    std::wstring line = BuildElevatedShareArgs(std::span(&src, 1), opt);
    std::vector<std::wstring> storage;
    std::vector<wchar_t*> argv = SplitArgs(line, storage);

    std::vector<media::ShareSource> outSources;
    media::AgentOptions outOpt;
    Check(ParseElevatedShareArgs(int(argv.size()), argv.data(), outSources, outOpt),
        "what one side builds, the other side accepts");
    Check(outSources.size() == 1 && outSources[0].targetId == src.targetId,
        "the monitor handle survives the hex round trip");
    Check(outSources[0].name == src.name, "so does a name with spaces, via hex encoding");
    Check(outOpt.fps == opt.fps && outOpt.bitrateMbps == opt.bitrateMbps &&
              outOpt.maxDim == opt.maxDim,
        "and all three numeric options");
}

void TestForeignArgsAreNotShareArgs() {
    std::printf("[share] unrelated command lines never trigger elevated-share mode...\n");
    std::wstring exe = L"deskhub.exe";
    std::wstring flag = L"--help";
    wchar_t* argv[] = {exe.data(), flag.data()};

    std::vector<media::ShareSource> outSources;
    media::AgentOptions outOpt;
    Check(!ParseElevatedShareArgs(2, argv, outSources, outOpt),
        "a normal launch falls through to the main menu");
}

void TestShareFlagWithoutSourcesIsRejected() {
    std::printf("[share] the flag alone, with no decodable source, is rejected...\n");
    std::wstring exe = L"deskhub.exe";
    std::wstring flag(kElevatedShareFlag);
    std::wstring srcFlag = L"--src";
    std::wstring badSrc = L"m:0:41";

    wchar_t* argv[] = {exe.data(), flag.data(), srcFlag.data(), badSrc.data()};
    std::vector<media::ShareSource> outSources;
    media::AgentOptions outOpt;
    Check(!ParseElevatedShareArgs(4, argv, outSources, outOpt),
        "a zero handle cannot name a monitor, so there is nothing to share");
}

void TestClampKeepsTheFirstSources() {
    std::printf("[share] more displays than the protocol carries get clamped, with a note...\n");
    std::vector<media::ShareSource> many;
    for (uint64_t i = 0; i < kMaxSources + 3; ++i) {
        media::ShareSource s;
        s.targetId = i + 1;
        many.push_back(s);
    }

    const ShareClampResult clamped = ClampShareSources(many);
    Check(clamped.clamped, "the caller is told to warn the user");
    Check(clamped.sources.size() == kMaxSources, "only the protocol maximum survives");
    Check(clamped.sources.front().targetId == 1 && clamped.sources.back().targetId == kMaxSources,
        "and it is the first N displays, in order");

    const ShareClampResult fits = ClampShareSources({many.begin(), many.begin() + 2});
    Check(!fits.clamped && fits.sources.size() == 2, "a small list passes through untouched");
}

}

void RunShareArgsTests() {
    TestArgsRoundTrip();
    TestForeignArgsAreNotShareArgs();
    TestShareFlagWithoutSourcesIsRejected();
    TestClampKeepsTheFirstSources();
}
