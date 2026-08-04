#pragma once
#include "deskhub/media/AgentTypes.h"
#include "deskhub/media/ShareSource.h"

#include <cstdint>
#include <cwchar>
#include <span>
#include <string>
#include <vector>

namespace deskhub {

inline constexpr wchar_t kElevatedShareFlag[] = L"--elevated-share";

namespace share_args {

inline std::wstring HexEncode(const std::string& s) {
    static const wchar_t* kDigits = L"0123456789abcdef";
    std::wstring out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) {
        out.push_back(kDigits[c >> 4]);
        out.push_back(kDigits[c & 0xF]);
    }
    return out;
}

inline bool HexDecode(const std::wstring& in, std::string& out) {
    if (in.size() % 2 != 0) return false;
    out.clear();
    out.reserve(in.size() / 2);
    for (size_t i = 0; i < in.size(); i += 2) {
        int hi = -1, lo = -1;
        for (int k = 0; k < 16; ++k) {
            if (in[i] == L"0123456789abcdef"[k]) hi = k;
            if (in[i + 1] == L"0123456789abcdef"[k]) lo = k;
        }
        if (hi < 0 || lo < 0) return false;
        out.push_back(char((hi << 4) | lo));
    }
    return true;
}

inline std::wstring EncodeSource(const media::ShareSource& s) {
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%llx", (unsigned long long)s.targetId);
    return std::wstring(L"m:") + buf + L":" + HexEncode(s.name);
}

inline bool DecodeSource(const std::wstring& tok, media::ShareSource& out) {
    if (tok.size() < 4 || tok[0] != L'm' || tok[1] != L':') return false;

    const size_t sep = tok.find(L':', 2);
    if (sep == std::wstring::npos) return false;

    const std::wstring hexHandle = tok.substr(2, sep - 2);
    const uint64_t handle = std::wcstoull(hexHandle.c_str(), nullptr, 16);
    if (handle == 0) return false;
    if (!HexDecode(tok.substr(sep + 1), out.name)) return false;

    out.targetId = handle;
    return true;
}

}

inline std::wstring BuildElevatedShareArgs(std::span<const media::ShareSource> sources,
    const media::AgentOptions& opt) {
    wchar_t nums[128];
    std::swprintf(nums, 128, L" --fps %u --bitrate %u --maxdim %u", unsigned(opt.fps),
        unsigned(opt.bitrateMbps), unsigned(opt.maxDim));

    std::wstring args = kElevatedShareFlag;
    args += nums;
    for (const auto& s : sources) args += L" --src " + share_args::EncodeSource(s);
    return args;
}

inline bool ParseElevatedShareArgs(int argc, wchar_t** argv,
    std::vector<media::ShareSource>& outSources, media::AgentOptions& outOpt) {
    bool isShare = false;
    for (int i = 1; i < argc; ++i)
        if (std::wcscmp(argv[i], kElevatedShareFlag) == 0) isShare = true;
    if (!isShare) return false;

    media::AgentOptions opt;
    std::vector<media::ShareSource> sources;

    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        const bool hasNext = (i + 1) < argc;
        if (a == L"--fps" && hasNext) {
            opt.fps = uint32_t(std::wcstoul(argv[++i], nullptr, 10));
        } else if (a == L"--bitrate" && hasNext) {
            opt.bitrateMbps = uint32_t(std::wcstoul(argv[++i], nullptr, 10));
        } else if (a == L"--maxdim" && hasNext) {
            opt.maxDim = uint32_t(std::wcstoul(argv[++i], nullptr, 10));
        } else if (a == L"--src" && hasNext) {
            media::ShareSource s;
            if (share_args::DecodeSource(argv[++i], s)) sources.push_back(std::move(s));
        }
    }

    if (sources.empty()) return false;
    outSources = std::move(sources);
    outOpt = opt;
    return true;
}

}
