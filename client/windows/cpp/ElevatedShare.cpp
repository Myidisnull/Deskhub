#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ElevatedShare.h"

#include <shellapi.h>

#include <cstdlib>
#include <cstring>

#pragma comment(lib, "shell32.lib")

namespace {

constexpr wchar_t kFlagShare[] = L"--elevated-share";

std::wstring HexEncode(const std::string& s) {
    static const wchar_t* kDigits = L"0123456789abcdef";
    std::wstring out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) {
        out.push_back(kDigits[c >> 4]);
        out.push_back(kDigits[c & 0xF]);
    }
    return out;
}

bool HexDecode(const std::wstring& in, std::string& out) {
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

std::wstring SelfPath() {
    wchar_t path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    return (n == 0 || n >= MAX_PATH) ? std::wstring() : std::wstring(path, n);
}

std::wstring EncodeSource(const AgentSource& s) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%llx", (unsigned long long)(uintptr_t)s.monitor);
    return std::wstring(L"m:") + buf + L":" + HexEncode(s.name);
}

bool DecodeSource(const std::wstring& tok, AgentSource& out) {
    if (tok.size() < 4 || tok[0] != L'm' || tok[1] != L':') return false;

    const size_t sep = tok.find(L':', 2);
    if (sep == std::wstring::npos) return false;

    const std::wstring hexHandle = tok.substr(2, sep - 2);
    const uintptr_t handle = (uintptr_t)wcstoull(hexHandle.c_str(), nullptr, 16);
    if (handle == 0) return false;
    if (!HexDecode(tok.substr(sep + 1), out.name)) return false;

    out.monitor = (HMONITOR)handle;
    return true;
}

}

bool IsProcessElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD len = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation,
                        sizeof(elevation), &len) != FALSE;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

bool RelaunchElevatedShare(std::span<const AgentSource> sources,
    const AgentOptions& opt, bool& outCancelled) {
    outCancelled = false;

    const std::wstring exe = SelfPath();
    if (exe.empty()) return false;

    wchar_t nums[128];
    swprintf(nums, 128, L" --fps %u --bitrate %u", unsigned(opt.fps), unsigned(opt.bitrateMbps));

    std::wstring args = kFlagShare;
    args += nums;
    for (const auto& s : sources) args += L" --src " + EncodeSource(s);

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (ShellExecuteExW(&sei)) {
        if (sei.hProcess) CloseHandle(sei.hProcess);
        return true;
    }
    outCancelled = GetLastError() == ERROR_CANCELLED;
    return false;
}

bool ParseElevatedShareArgs(int adeskhub, wchar_t** argv,
    std::vector<AgentSource>& outSources, AgentOptions& outOpt) {
    bool isShare = false;
    for (int i = 1; i < adeskhub; ++i)
        if (wcscmp(argv[i], kFlagShare) == 0) isShare = true;
    if (!isShare) return false;

    AgentOptions opt;
    std::vector<AgentSource> sources;

    for (int i = 1; i < adeskhub; ++i) {
        const std::wstring a = argv[i];
        const bool hasNext = (i + 1) < adeskhub;
        if (a == L"--fps" && hasNext) {
            opt.fps = uint32_t(wcstoul(argv[++i], nullptr, 10));
        } else if (a == L"--bitrate" && hasNext) {
            opt.bitrateMbps = uint32_t(wcstoul(argv[++i], nullptr, 10));
        } else if (a == L"--src" && hasNext) {
            AgentSource s;
            if (DecodeSource(argv[++i], s)) sources.push_back(std::move(s));
        }
    }

    if (sources.empty()) return false;
    outSources = std::move(sources);
    outOpt = opt;
    return true;
}
