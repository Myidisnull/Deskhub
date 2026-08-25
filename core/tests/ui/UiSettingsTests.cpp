#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/ui/UiSettings.h"

#include <cstdio>
#include <string>

using namespace deskhub;

namespace {

void TestRoundTrip() {
    std::printf("[settings] saved settings come back exactly as chosen...\n");
    ui::UiSettings s;
    s.fps = 30;
    s.bitrateMbps = 50;
    s.maxDim = 2560;
    s.port = 50123;
    s.allowInput = false;
    s.clientControl = false;
    s.runInBackground = true;
    s.runInBackgroundChoiceMade = true;
    s.hideTrayIcon = true;
    s.logMaxFileMb = 25;
    s.logCompressAfterDays = 3;
    s.logDeleteAfterDays = 14;
    s.logDir = "/tmp/deskhub-logs";
    s.passcode = "0417";
    s.deviceName = "Anh's laptop";
    s.bindIp = "192.168.1.10";
    s.autostart = true;
    s.autoShare = true;
    s.clipboardSync = true;
    s.keepAwake = false;
    s.encryptSession = true;
    s.escrowSessionKey = true;
    s.sessionKeyLifetime = ui::SessionKeyLifetime::Persistent;
    s.sessionKeyHex = std::string(64, 'a');
    s.hostStaticSkHex = std::string(64, 'b');
    s.language = "zh-Hans";
    Check(ui::ParseUiSettings(ui::SerializeUiSettings(s)) == s,
        "serialize then parse is identity");
}

void TestEncryptSessionPersistence() {
    std::printf("[settings] encrypt-session fields round-trip and gate correctly...\n");
    const ui::UiSettings defaults;
    Check(!defaults.encryptSession && !defaults.escrowSessionKey, "encrypt defaults off");
    Check(defaults.sessionKeyLifetime == ui::SessionKeyLifetime::PerShare,
        "lifetime defaults to per-share");
    Check(defaults.sessionKeyHex.empty() && defaults.hostStaticSkHex.empty(),
        "no keys by default");

    Check(ui::ParseUiSettings("encrypt_session=1").encryptSession, "encrypt round-trips on");
    Check(!ui::ParseUiSettings("encrypt_session=0").encryptSession, "and off");
    Check(ui::ParseUiSettings("escrow_session_key=1").escrowSessionKey, "escrow parses on");
    Check(ui::ParseUiSettings("session_key_lifetime=1").sessionKeyLifetime ==
              ui::SessionKeyLifetime::Persistent,
        "persistent lifetime round-trips");
    Check(ui::ParseUiSettings("session_key_lifetime=0").sessionKeyLifetime ==
              ui::SessionKeyLifetime::PerShare,
        "per-share lifetime round-trips");
    Check(ui::ParseUiSettings("session_key_lifetime=9").sessionKeyLifetime ==
              ui::SessionKeyLifetime::PerShare,
        "unknown lifetime falls back to per-share");

    const std::string keyA(64, 'c');
    const std::string keyB(64, 'd');
    ui::UiSettings on;
    on.encryptSession = true;
    on.escrowSessionKey = true;
    on.sessionKeyLifetime = ui::SessionKeyLifetime::Persistent;
    on.sessionKeyHex = keyA;
    on.hostStaticSkHex = keyB;
    const ui::UiSettings back = ui::ParseUiSettings(ui::SerializeUiSettings(on));
    Check(back.encryptSession && back.escrowSessionKey, "encrypt+escrow survive serialize");
    Check(back.sessionKeyLifetime == ui::SessionKeyLifetime::Persistent, "lifetime survives");
    Check(back.sessionKeyHex == keyA && back.hostStaticSkHex == keyB, "keys survive");

    ui::UiSettings off = on;
    off.encryptSession = false;
    const std::string offText = ui::SerializeUiSettings(off);
    const ui::UiSettings offBack = ui::ParseUiSettings(offText);
    Check(!offBack.encryptSession, "turning encrypt off persists");
    Check(!offBack.escrowSessionKey, "escrow is cleared when encrypt is off");
    Check(offBack.sessionKeyLifetime == ui::SessionKeyLifetime::PerShare,
        "lifetime resets when encrypt is off");
    Check(offBack.sessionKeyHex.empty(), "session key is not written when encrypt is off");
    Check(offBack.hostStaticSkHex == keyB, "host static key still persists");
    Check(offText.find("escrow_session_key=0") != std::string::npos, "escrow writes as off");

    Check(ui::ParseUiSettings("session_key=short").sessionKeyHex.empty(),
        "short session keys are dropped");
    Check(ui::ParseUiSettings("host_static_sk=short").hostStaticSkHex.empty(),
        "short host keys are dropped");
}

void TestBindIpPersistence() {
    std::printf("[settings] the bind address persists only when it is a real IPv4...\n");
    Check(ui::ParseUiSettings("").bindIp.empty(), "all interfaces by default");
    Check(ui::ParseUiSettings("bind_ip=192.168.1.10").bindIp == "192.168.1.10",
        "a real address is kept");
    Check(ui::ParseUiSettings("bind_ip=").bindIp.empty(), "an empty value means all interfaces");
    Check(ui::ParseUiSettings("bind_ip=not-an-ip").bindIp.empty(), "junk is dropped");
    Check(ui::ParseUiSettings("bind_ip=192.168.1.999").bindIp.empty(),
        "an out-of-range octet is dropped");

    ui::UiSettings s;
    s.bindIp = "garbage";
    Check(ui::ParseUiSettings(ui::SerializeUiSettings(s)).bindIp.empty(),
        "an invalid in-memory address is not written out");
}

void TestBehaviorTogglesPersist() {
    std::printf("[settings] the launch and clipboard toggles default off and round-trip...\n");
    const ui::UiSettings defaults;
    Check(!defaults.autostart && !defaults.autoShare && !defaults.clipboardSync,
        "every new toggle defaults off");
    Check(ui::ParseUiSettings("autostart=1").autostart, "autostart round-trips on");
    Check(!ui::ParseUiSettings("autostart=0").autostart, "and off");
    Check(ui::ParseUiSettings("auto_share=1").autoShare, "auto-share round-trips on");
    Check(ui::ParseUiSettings("clipboard_sync=1").clipboardSync,
        "clipboard sync round-trips on");
    Check(ui::ParseUiSettings("start_hidden=1").runInBackground,
        "legacy start_hidden migrates to run-in-background");
    Check(ui::ParseUiSettings("start_hidden=1").runInBackgroundChoiceMade,
        "legacy start_hidden records the background choice");
    Check(ui::SerializeUiSettings(ui::ParseUiSettings("start_hidden=1")).find("start_hidden=") ==
              std::string::npos,
        "start_hidden is no longer written out");
    Check(!ui::ParseUiSettings("autostart=x").autostart,
        "junk in a toggle falls back to off");
}

void TestDeviceNamePersistence() {
    std::printf("[settings] the device name persists, trimmed to its limits...\n");
    Check(ui::ParseUiSettings("").deviceName.empty(), "no name by default");
    Check(ui::ParseUiSettings("name=Ph\xC3\xB2ng kh\xC3\xA1"
                              "ch")
                  .deviceName ==
              "Ph\xC3\xB2ng kh\xC3\xA1"
              "ch",
        "a hand-written UTF-8 name is kept");

    std::string longName;
    while (longName.size() < kMaxClientNameBytes + 20) longName += "\xE1\xBA\xA1";
    const std::string parsed = ui::ParseUiSettings("name=" + longName).deviceName;
    Check(parsed.size() <= kMaxClientNameBytes && parsed.size() % 3 == 0,
        "an over-long name is cut on a UTF-8 boundary");

    Check(ui::TruncateDeviceName("a\x01"
                                 "b\x7f"
                                 "c") == "abc",
        "control characters never reach the file or the wire");
    Check(ui::TruncateDeviceName("") == "", "an empty name stays empty");

    ui::UiSettings s;
    s.deviceName = "desk\x02top";
    Check(ui::ParseUiSettings(ui::SerializeUiSettings(s)).deviceName == "desktop",
        "a dirty in-memory name is cleaned on the way out");
}

void TestPasscodePersistence() {
    std::printf("[settings] the passcode persists only when it is 4 digits...\n");
    ui::UiSettings s;
    s.passcode = "0417";
    const std::string text = ui::SerializeUiSettings(s);
    Check(ui::ParseUiSettings(text).passcode == "0417",
        "a 4-digit passcode round-trips, leading zero kept");
    Check(text.find("passcode=0417") == std::string::npos,
        "the file never carries the digits in the clear");

    Check(ui::ParseUiSettings("").passcode.empty(), "no passcode by default");
    Check(ui::ParseUiSettings("passcode=0417").passcode == "0417",
        "a hand-written passcode still works");
    Check(ui::ParseUiSettings("passcode=123").passcode.empty(), "too short is dropped");
    Check(ui::ParseUiSettings("passcode=12345").passcode.empty(), "too long is dropped");
    Check(ui::ParseUiSettings("passcode=12ab").passcode.empty(), "non-digits are dropped");

    s.passcode = "not4";
    Check(ui::ParseUiSettings(ui::SerializeUiSettings(s)).passcode.empty(),
        "an invalid in-memory passcode is not written out");
}

void TestDefaultsMatchShareDefaults() {
    std::printf("[settings] a missing or empty file yields the share defaults...\n");
    const ui::UiSettings defaults;
    Check(defaults.fps == 60 && defaults.bitrateMbps == 20 && defaults.maxDim == 1920,
        "defaults are the long-standing share defaults");
    Check(defaults.port == kDeskhubPort, "the default port is the protocol default");
    Check(defaults.allowInput, "remote control is allowed unless the user turns it off");
    Check(!defaults.runInBackground, "closing quits until the user opts into the tray");
    Check(!defaults.runInBackgroundChoiceMade, "the first close still asks about the tray");
    Check(!defaults.hideTrayIcon, "the tray icon is shown unless the user hides it");
    Check(!defaults.autoShare, "sharing still waits for an explicit start by default");
    Check(defaults.keepAwake, "keep awake defaults on");
    Check(defaults.shareAudio, "share audio defaults on");
    Check(defaults.playAudio, "play audio defaults on");
    Check(defaults.logMaxFileMb == 10 && defaults.logCompressAfterDays == 7 &&
              defaults.logDeleteAfterDays == 30,
        "log retention defaults match common desktop logging practice");
    Check(defaults.logDir.empty(), "log directory defaults to the built-in Deskhub folder");
    Check(ui::ParseUiSettings("") == defaults, "empty text is all defaults");
    Check(!ui::ParseUiSettings("keep_awake=0").keepAwake, "keep awake round-trips off");
    Check(ui::ParseUiSettings("keep_awake=1").keepAwake, "and on");
    Check(ui::ParseUiSettings("keep_awake=x").keepAwake,
        "junk leaves the keep-awake default alone");
    Check(!ui::ParseUiSettings("share_audio=0").shareAudio, "share audio round-trips off");
    Check(ui::ParseUiSettings("share_audio=1").shareAudio, "and on");
    Check(!ui::ParseUiSettings("play_audio=0").playAudio, "play audio round-trips off");
    Check(ui::ParseUiSettings("play_audio=1").playAudio, "and on");
    Check(!ui::ParseUiSettings("accept_files=0").acceptFiles, "accept files round-trips off");
    Check(ui::ParseUiSettings("accept_files=1").acceptFiles, "and on");
    Check(!ui::ParseUiSettings("share_terminal=0").shareTerminal, "share terminal round-trips off");
    Check(ui::ParseUiSettings("share_terminal=1").shareTerminal, "and on");
    Check(!ui::ParseUiSettings("allow_new_pairings=0").allowNewPairings,
        "allow new pairings round-trips off");
    Check(ui::ParseUiSettings("allow_new_pairings=1").allowNewPairings, "and on");
}

void TestNativeQualityIsPreserved() {
    std::printf("[settings] the Native preset (max_dim=0) survives a round trip...\n");
    ui::UiSettings s;
    s.maxDim = 0;
    Check(ui::ParseUiSettings(ui::SerializeUiSettings(s)).maxDim == 0,
        "zero means native resolution, not a parse failure");
}

void TestGarbageFallsBackPerKey() {
    std::printf("[settings] each bad value falls back alone, good ones still apply...\n");
    const ui::UiSettings s = ui::ParseUiSettings(
        "fps=abc\n"
        "bitrate_mbps=50\n"
        "max_dim=999999\n"
        "unknown_key=7\n"
        "no equals sign here\n");
    Check(s.fps == 60, "junk fps falls back to the default");
    Check(s.bitrateMbps == 50, "the valid bitrate is kept");
    Check(s.maxDim == 1920, "an absurd dimension falls back to the default");
}

void TestBoundsAreEnforced() {
    std::printf("[settings] zero and overflow values cannot smuggle in...\n");
    Check(ui::ParseUiSettings("fps=0").fps == 60, "fps zero is rejected");
    Check(ui::ParseUiSettings("fps=241").fps == 60, "fps above the cap is rejected");
    Check(ui::ParseUiSettings("bitrate_mbps=0").bitrateMbps == 20, "bitrate zero is rejected");
    Check(ui::ParseUiSettings("port=0").port == kDeskhubPort, "port zero is rejected");
    Check(ui::ParseUiSettings("port=65536").port == kDeskhubPort,
        "a port past 16 bits is rejected");
    Check(ui::ParseUiSettings("port=50123").port == 50123, "a real custom port is kept");
    Check(!ui::ParseUiSettings("allow_input=0").allowInput, "view-only mode round-trips");
    Check(ui::ParseUiSettings("allow_input=1").allowInput, "so does control mode");
    Check(!ui::ParseUiSettings("client_control=0").clientControl,
        "the client-side view-only choice round-trips too");
    Check(ui::ParseUiSettings("").clientControl, "and defaults to controlling");
    Check(ui::ParseUiSettings("allow_input=x").allowInput,
        "junk falls back to allowing control, the long-standing behaviour");
    Check(ui::ParseUiSettings("run_in_background=1").runInBackground, "tray preference round-trips");
    Check(ui::ParseUiSettings("run_in_background_choice_made=1").runInBackgroundChoiceMade,
        "so does the recorded-choice flag");
    Check(ui::ParseUiSettings("hide_tray_icon=1").hideTrayIcon, "hiding the tray round-trips too");
    Check(ui::ParseUiSettings("share_on_launch=1").autoShare, "share-on-launch round-trips");
    Check(!ui::ParseUiSettings("").autoShare, "share-on-launch stays off by default");
    Check(ui::ParseUiSettings("auto_share=1").autoShare, "auto-share round-trips");
    Check(ui::ParseUiSettings("log_max_file_mb=20").logMaxFileMb == 20,
        "log size ceiling round-trips");
    Check(ui::ParseUiSettings("log_max_file_mb=0").logMaxFileMb == 10,
        "log size zero is rejected");
    Check(ui::ParseUiSettings("log_compress_after_days=0").logCompressAfterDays == 0,
        "compress never is allowed");
    Check(ui::ParseUiSettings("log_delete_after_days=2\nlog_compress_after_days=9")
                  .logDeleteAfterDays == 9,
        "delete cannot be earlier than compress");
    Check(ui::ParseUiSettings("log_dir=/var/log/deskhub").logDir == "/var/log/deskhub",
        "a custom log directory round-trips");
    Check(ui::ParseUiSettings("log_dir=\n").logDir.empty(), "blank log directory means default");
    Check(ui::ParseUiSettings("fps = 30 ").fps == 30, "spaces around key and value are fine");
    Check(ui::ParseUiSettings("language=zh-Hans").language == "zh-Hans", "language round-trips");
    Check(ui::ParseUiSettings("language=fr").language == "fr", "french code is kept");
    Check(ui::ParseUiSettings("language=").language.empty(), "blank language means system");
    Check(ui::ParseUiSettings("language=nope").language.empty(), "junk language is dropped");
    ui::UiSettings lang;
    lang.language = "ja";
    Check(ui::ParseUiSettings(ui::SerializeUiSettings(lang)).language == "ja",
        "serialize keeps language");
}

}

void RunUiSettingsTests() {
    TestRoundTrip();
    TestEncryptSessionPersistence();
    TestBindIpPersistence();
    TestBehaviorTogglesPersist();
    TestDeviceNamePersistence();
    TestPasscodePersistence();
    TestDefaultsMatchShareDefaults();
    TestNativeQualityIsPreserved();
    TestGarbageFallsBackPerKey();
    TestBoundsAreEnforced();
}
