#include "deskhub/ui/RecentDevices.h"

#include <algorithm>
#include <climits>
#include <cstdint>

#include "deskhub/crypto/KeyCodec.h"
#include "deskhub/ui/SecretText.h"
#include "deskhub/ui/Strings.h"

namespace deskhub::ui {

namespace {

bool ParseUnixTime(std::string_view s, int64_t& out) {
    if (s.empty()) return false;
    int64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        if (v > (INT64_MAX - 9) / 10) return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

bool ParseLine(std::string_view line, RecentDevice& out) {
    const std::string trimmed = TrimAscii(line);
    const size_t space = trimmed.find(' ');
    if (space == std::string::npos) return false;

    int64_t stamp = 0;
    if (!ParseUnixTime(std::string_view(trimmed).substr(0, space), stamp)) return false;

    std::string rest = TrimAscii(std::string_view(trimmed).substr(space + 1));
    if (rest.empty()) return false;

    std::string addr;
    std::string passcode;
    bool encrypted = false;
    std::string sessionKey;
    size_t pos = 0;
    while (pos < rest.size()) {
        while (pos < rest.size() && rest[pos] == ' ') ++pos;
        if (pos >= rest.size()) break;
        size_t end = rest.find(' ', pos);
        if (end == std::string::npos) end = rest.size();
        const std::string_view tok(rest.data() + pos, end - pos);
        pos = end;
        if (addr.empty()) {
            addr = std::string(tok);
            continue;
        }
        if (tok.size() >= 2 && tok[0] == '@') {
            const std::string decoded = DecodeSecret(tok.substr(1));
            if (IsValidPasscode(decoded)) passcode = decoded;
            continue;
        }
        if (tok == "!1") {
            encrypted = true;
            continue;
        }
        if (tok.size() >= 2 && tok[0] == '#') {
            const std::string decoded = DecodeSecret(tok.substr(1));
            uint8_t key[crypto::kKeySize];
            if (crypto::KeyFromHex(decoded, std::span<uint8_t>(key, crypto::kKeySize))) {
                sessionKey = decoded;
                encrypted = true;
            }
            crypto::SecureWipe(std::span<uint8_t>(key, crypto::kKeySize));
            continue;
        }
        const std::string decoded = DecodeSecret(tok);
        if (IsValidPasscode(decoded) && passcode.empty()) passcode = decoded;
    }
    if (addr.empty()) return false;

    out.addr = std::move(addr);
    out.lastConnectedUnix = stamp;
    out.passcode = std::move(passcode);
    out.encrypted = encrypted;
    out.sessionKey = std::move(sessionKey);
    return true;
}

bool ContainsAddr(const std::vector<RecentDevice>& devices, std::string_view addr) {
    return std::any_of(devices.begin(), devices.end(),
        [&](const RecentDevice& d) { return d.addr == addr; });
}

}

std::vector<RecentDevice> ParseRecentDevices(std::string_view text) {
    std::vector<RecentDevice> out;
    size_t pos = 0;
    while (pos < text.size() && out.size() < kMaxRecentDevices) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();

        RecentDevice device;
        if (ParseLine(text.substr(pos, end - pos), device) && !ContainsAddr(out, device.addr))
            out.push_back(std::move(device));

        pos = end + 1;
    }
    return out;
}

std::string SerializeRecentDevices(const std::vector<RecentDevice>& devices) {
    std::string out;
    size_t count = 0;
    for (const RecentDevice& d : devices) {
        if (count == kMaxRecentDevices) break;
        if (d.addr.empty()) continue;
        out += std::to_string(d.lastConnectedUnix);
        out += ' ';
        out += d.addr;
        if (IsValidPasscode(d.passcode)) {
            out += " @";
            out += EncodeSecret(d.passcode);
        }
        if (d.encrypted) out += " !1";
        uint8_t key[crypto::kKeySize];
        if (d.encrypted && crypto::KeyFromHex(d.sessionKey, std::span<uint8_t>(key, crypto::kKeySize))) {
            out += " #";
            out += EncodeSecret(d.sessionKey);
        }
        crypto::SecureWipe(std::span<uint8_t>(key, crypto::kKeySize));
        out += '\n';
        ++count;
    }
    return out;
}

void TouchRecentDevice(std::vector<RecentDevice>& devices, std::string_view addr,
    int64_t nowUnix, std::string_view passcode, bool encrypted, std::string_view sessionKey) {
    const std::string trimmed = TrimAscii(addr);
    if (trimmed.empty()) return;

    std::string keptPasscode;
    for (const RecentDevice& d : devices) {
        if (d.addr == trimmed) {
            keptPasscode = d.passcode;
            break;
        }
    }

    RemoveRecentDevice(devices, trimmed);
    RecentDevice row;
    row.addr = trimmed;
    row.lastConnectedUnix = nowUnix;
    row.passcode = IsValidPasscode(passcode) ? std::string(passcode) : std::move(keptPasscode);
    row.encrypted = encrypted;
    uint8_t key[crypto::kKeySize];
    if (encrypted && crypto::KeyFromHex(sessionKey, std::span<uint8_t>(key, crypto::kKeySize)))
        row.sessionKey = std::string(sessionKey);
    crypto::SecureWipe(std::span<uint8_t>(key, crypto::kKeySize));
    devices.insert(devices.begin(), std::move(row));
    if (devices.size() > kMaxRecentDevices) devices.resize(kMaxRecentDevices);
}

std::string PasscodeForDevice(const std::vector<RecentDevice>& devices, std::string_view addr) {
    const std::string trimmed = TrimAscii(addr);
    for (const RecentDevice& d : devices)
        if (d.addr == trimmed) return d.passcode;
    return {};
}

std::string SessionKeyForDevice(const std::vector<RecentDevice>& devices, std::string_view addr) {
    const std::string trimmed = TrimAscii(addr);
    for (const RecentDevice& d : devices)
        if (d.addr == trimmed) return d.sessionKey;
    return {};
}

bool EncryptedForDevice(const std::vector<RecentDevice>& devices, std::string_view addr) {
    const std::string trimmed = TrimAscii(addr);
    for (const RecentDevice& d : devices)
        if (d.addr == trimmed) return d.encrypted;
    return false;
}

void RemoveRecentDevice(std::vector<RecentDevice>& devices, std::string_view addr) {
    devices.erase(std::remove_if(devices.begin(), devices.end(),
                      [&](const RecentDevice& d) { return d.addr == addr; }),
        devices.end());
}

}
