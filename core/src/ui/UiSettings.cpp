#include "deskhub/ui/UiSettings.h"

#include <optional>

#include "deskhub/net/Ipv4.h"
#include "deskhub/ui/Locale.h"
#include "deskhub/ui/SecretText.h"
#include "deskhub/ui/Strings.h"

namespace deskhub::ui {

namespace {

std::optional<uint32_t> ParseUint(std::string_view s) {
    if (s.empty()) return std::nullopt;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        v = v * 10 + uint64_t(c - '0');
        if (v > 0xFFFFFFFFull) return std::nullopt;
    }
    return uint32_t(v);
}

void ApplyKeyValue(UiSettings& out, std::string_view key, std::string_view value) {
    if (key == "passcode") {
        const std::string decoded = DecodeSecret(value);
        if (IsValidPasscode(decoded)) out.passcode = decoded;
        return;
    }
    if (key == "name") {
        out.deviceName = TruncateDeviceName(value);
        return;
    }
    if (key == "bind_ip") {
        if (value.empty() || ParseIPv4(value)) out.bindIp = value;
        return;
    }
    if (key == "log_dir") {
        const std::string dir = TrimAscii(value);
        if (diag::IsPlausibleLogDir(dir)) out.logDir = dir;
        return;
    }
    if (key == "host_static_sk") {
        const std::string decoded = DecodeSecret(value);
        if (decoded.size() == 64) out.hostStaticSkHex = decoded;
        return;
    }
    if (key == "session_key") {
        const std::string decoded = DecodeSecret(value);
        if (decoded.size() == 64) out.sessionKeyHex = decoded;
        return;
    }
    if (key == "language") {
        const std::string code = TrimAscii(value);
        UiLanguage parsed = UiLanguage::System;
        if (!TryParseLanguageCode(code, parsed)) return;
        out.language = parsed == UiLanguage::System ? std::string{} : LanguageCode(parsed);
        return;
    }

    const std::optional<uint32_t> v = ParseUint(value);
    if (!v) return;

    if (key == "fps" && *v >= 1 && *v <= kMaxSettingsFps) out.fps = *v;
    if (key == "bitrate_mbps" && *v >= 1 && *v <= kMaxSettingsBitrateMbps) out.bitrateMbps = *v;
    if (key == "max_dim" && *v <= kMaxSettingsDim) out.maxDim = *v;
    if (key == "port" && *v >= 1 && *v <= kMaxSettingsPort) out.port = *v;
    if (key == "allow_input") out.allowInput = *v != 0;
    if (key == "client_control") out.clientControl = *v != 0;
    if (key == "run_in_background") out.runInBackground = *v != 0;
    if (key == "run_in_background_choice_made") out.runInBackgroundChoiceMade = *v != 0;
    if (key == "hide_tray_icon") out.hideTrayIcon = *v != 0;
    if (key == "autostart") out.autostart = *v != 0;
    if (key == "auto_share" || key == "share_on_launch") out.autoShare = *v != 0;
    if (key == "clipboard_sync") out.clipboardSync = *v != 0;
    if (key == "keep_awake") out.keepAwake = *v != 0;
    if (key == "share_audio") out.shareAudio = *v != 0;
    if (key == "play_audio") out.playAudio = *v != 0;
    if (key == "encrypt_session") out.encryptSession = *v != 0;
    if (key == "escrow_session_key") out.escrowSessionKey = *v != 0;
    if (key == "session_key_lifetime")
        out.sessionKeyLifetime =
            *v == uint32_t(SessionKeyLifetime::Persistent) ? SessionKeyLifetime::Persistent
                                                           : SessionKeyLifetime::PerShare;
    if (key == "start_hidden" && *v != 0) {
        out.runInBackground = true;
        out.runInBackgroundChoiceMade = true;
    }
    if (key == "log_max_file_mb" && *v >= diag::kMinLogMaxFileMb &&
        *v <= diag::kMaxLogMaxFileMb)
        out.logMaxFileMb = *v;
    if (key == "log_compress_after_days" && *v <= diag::kMaxLogRetentionDays)
        out.logCompressAfterDays = *v;
    if (key == "log_delete_after_days" && *v <= diag::kMaxLogRetentionDays)
        out.logDeleteAfterDays = *v;
}

}

std::string TruncateDeviceName(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name)
        if (uint8_t(c) >= 0x20 && uint8_t(c) != 0x7F) out.push_back(c);
    if (out.size() > kMaxClientNameBytes) {
        size_t n = kMaxClientNameBytes;
        while (n > 0 && (uint8_t(out[n]) & 0xC0) == 0x80) --n;
        out.resize(n);
    }
    return out;
}

UiSettings ParseUiSettings(std::string_view text) {
    UiSettings out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();

        const std::string line = TrimAscii(text.substr(pos, end - pos));
        const size_t eq = line.find('=');
        if (eq != std::string::npos) {
            const std::string key = TrimAscii(std::string_view(line).substr(0, eq));
            const std::string value = TrimAscii(std::string_view(line).substr(eq + 1));
            ApplyKeyValue(out, key, value);
        }

        pos = end + 1;
    }
    const diag::LogPolicy log = out.LogPolicy();
    out.logMaxFileMb = log.maxFileMb;
    out.logCompressAfterDays = log.compressAfterDays;
    out.logDeleteAfterDays = log.deleteAfterDays;
    return out;
}

std::string SerializeUiSettings(const UiSettings& settings) {
    std::string out;
    out += "fps=" + std::to_string(settings.fps) + '\n';
    out += "bitrate_mbps=" + std::to_string(settings.bitrateMbps) + '\n';
    out += "max_dim=" + std::to_string(settings.maxDim) + '\n';
    out += "port=" + std::to_string(settings.port) + '\n';
    out += std::string("allow_input=") + (settings.allowInput ? "1" : "0") + '\n';
    out += std::string("client_control=") + (settings.clientControl ? "1" : "0") + '\n';
    out += std::string("run_in_background=") + (settings.runInBackground ? "1" : "0") + '\n';
    out += std::string("run_in_background_choice_made=") +
           (settings.runInBackgroundChoiceMade ? "1" : "0") + '\n';
    out += std::string("hide_tray_icon=") + (settings.hideTrayIcon ? "1" : "0") + '\n';
    const diag::LogPolicy log = settings.LogPolicy();
    out += "log_max_file_mb=" + std::to_string(log.maxFileMb) + '\n';
    out += "log_compress_after_days=" + std::to_string(log.compressAfterDays) + '\n';
    out += "log_delete_after_days=" + std::to_string(log.deleteAfterDays) + '\n';
    out += "log_dir=";
    if (diag::IsPlausibleLogDir(settings.logDir) && !settings.logDir.empty())
        out += settings.logDir;
    out += '\n';
    out += "passcode=";
    if (IsValidPasscode(settings.passcode)) out += EncodeSecret(settings.passcode);
    out += '\n';
    out += "name=" + TruncateDeviceName(settings.deviceName) + '\n';
    out += "bind_ip=";
    if (ParseIPv4(settings.bindIp)) out += settings.bindIp;
    out += '\n';
    out += std::string("autostart=") + (settings.autostart ? "1" : "0") + '\n';
    out += std::string("auto_share=") + (settings.autoShare ? "1" : "0") + '\n';
    out += std::string("clipboard_sync=") + (settings.clipboardSync ? "1" : "0") + '\n';
    out += std::string("keep_awake=") + (settings.keepAwake ? "1" : "0") + '\n';
    out += std::string("share_audio=") + (settings.shareAudio ? "1" : "0") + '\n';
    out += std::string("play_audio=") + (settings.playAudio ? "1" : "0") + '\n';
    out += std::string("encrypt_session=") + (settings.encryptSession ? "1" : "0") + '\n';
    const bool escrow = settings.encryptSession && settings.escrowSessionKey;
    out += std::string("escrow_session_key=") + (escrow ? "1" : "0") + '\n';
    out += "session_key_lifetime=" +
           std::to_string(uint32_t(settings.encryptSession ? settings.sessionKeyLifetime
                                                           : SessionKeyLifetime::PerShare)) +
           '\n';
    out += "session_key=";
    if (settings.encryptSession && settings.sessionKeyHex.size() == 64)
        out += EncodeSecret(settings.sessionKeyHex);
    out += '\n';
    out += "host_static_sk=";
    if (settings.hostStaticSkHex.size() == 64) out += EncodeSecret(settings.hostStaticSkHex);
    out += '\n';
    out += "language=";
    if (!settings.language.empty()) {
        const UiLanguage parsed = ParseLanguageCode(settings.language);
        if (parsed != UiLanguage::System) out += LanguageCode(parsed);
    }
    out += '\n';
    return out;
}

}
