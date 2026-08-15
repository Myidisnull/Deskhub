#include "deskhub/net/PairedDevices.h"

#include <algorithm>

namespace deskhub {

namespace {

std::string Trim(std::string_view s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

bool ParseUnixTime(std::string_view s, int64_t& out) {
    if (s.empty() || s.size() > 19) return false;
    int64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

std::string SanitizeName(std::string_view name) {
    std::string out;
    for (char c : name) {
        const uint8_t u = uint8_t(c);
        if (u >= 0x20 && u != 0x7F) out.push_back(c);
        if (out.size() == kMaxPairedNameBytes) break;
    }
    return Trim(out);
}

}

PairVerdict PairedDevices::Check(const Fingerprint& fp) const {
    if (IsZero(fp)) return PairVerdict::Unknown;
    for (const PairedDevice& d : devices_)
        if (d.fingerprint == fp) return PairVerdict::Paired;
    return PairVerdict::Unknown;
}

void PairedDevices::Remember(const Fingerprint& fp, std::string_view name, int64_t nowUnix) {
    if (IsZero(fp)) return;
    if (Touch(fp, name, nowUnix)) return;
    Insert(PairedDevice{fp, SanitizeName(name), nowUnix, nowUnix});
}

void PairedDevices::Insert(const PairedDevice& device) {
    if (IsZero(device.fingerprint)) return;
    Forget(device.fingerprint);
    if (devices_.size() >= kMaxPairedDevices) {
        const auto oldest = std::min_element(devices_.begin(), devices_.end(),
            [](const PairedDevice& a, const PairedDevice& b) {
                return a.lastSeenUnix < b.lastSeenUnix;
            });
        devices_.erase(oldest);
    }
    PairedDevice stored = device;
    stored.name = SanitizeName(device.name);
    devices_.push_back(std::move(stored));
}

bool PairedDevices::Touch(const Fingerprint& fp, std::string_view name, int64_t nowUnix) {
    if (IsZero(fp)) return false;
    for (PairedDevice& d : devices_) {
        if (!(d.fingerprint == fp)) continue;
        const std::string clean = SanitizeName(name);
        if (!clean.empty()) d.name = clean;
        d.lastSeenUnix = nowUnix;
        return true;
    }
    return false;
}

bool PairedDevices::Forget(const Fingerprint& fp) {
    const auto at = std::remove_if(devices_.begin(), devices_.end(),
        [&](const PairedDevice& d) { return d.fingerprint == fp; });
    if (at == devices_.end()) return false;
    devices_.erase(at, devices_.end());
    return true;
}

void PairedDevices::Clear() {
    devices_.clear();
}

std::optional<PairedDevice> PairedDevices::Find(const Fingerprint& fp) const {
    if (IsZero(fp)) return std::nullopt;
    for (const PairedDevice& d : devices_)
        if (d.fingerprint == fp) return d;
    return std::nullopt;
}

std::string ShortFingerprint(const Fingerprint& fp) {
    if (IsZero(fp)) return {};
    const std::string full = FormatFingerprint(fp);
    const size_t start = kFingerprintPrefix.size();
    if (full.size() <= start) return full;
    return full.substr(start, kShortFingerprintChars);
}

PairedDevices ParsePairedDevices(std::string_view text) {
    PairedDevices out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string line = Trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == '#') continue;

        const size_t s1 = line.find(' ');
        if (s1 == std::string::npos) continue;
        const size_t s2 = line.find(' ', s1 + 1);
        if (s2 == std::string::npos) continue;
        const size_t s3 = line.find(' ', s2 + 1);

        const std::optional<Fingerprint> fp = ParseFingerprint(line.substr(0, s1));
        if (!fp) continue;
        int64_t paired = 0;
        int64_t lastSeen = 0;
        if (!ParseUnixTime(std::string_view(line).substr(s1 + 1, s2 - s1 - 1), paired)) continue;
        const size_t stampEnd = s3 == std::string::npos ? line.size() : s3;
        if (!ParseUnixTime(std::string_view(line).substr(s2 + 1, stampEnd - s2 - 1), lastSeen))
            continue;

        const std::string name =
            s3 == std::string::npos ? std::string() : SanitizeName(line.substr(s3 + 1));
        out.Insert(PairedDevice{*fp, name, paired, lastSeen});
    }
    return out;
}

std::string SerializePairedDevices(const PairedDevices& devices) {
    std::string out;
    for (const PairedDevice& d : devices.Devices()) {
        if (IsZero(d.fingerprint)) continue;
        out += FormatFingerprint(d.fingerprint);
        out += ' ';
        out += std::to_string(d.pairedUnix);
        out += ' ';
        out += std::to_string(d.lastSeenUnix);
        if (!d.name.empty()) {
            out += ' ';
            out += d.name;
        }
        out += '\n';
    }
    return out;
}

}
