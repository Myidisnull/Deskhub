#include "deskhubp/ffi/TerminalFfi.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "deskhub/ui/Strings.h"
#include "deskhubp/ffi/TermGridFill.h"
#include "deskhubp/session/TerminalViewer.h"
#include "deskhubp/system/UiSettingsStore.h"

namespace {

int FillText(char* out, int capacity, const std::string& text) {
    if (out == nullptr || capacity <= 0) return int(text.size());
    const size_t take = std::min(size_t(capacity - 1), text.size());
    std::memcpy(out, text.data(), take);
    out[take] = '\0';
    return int(take);
}

}

struct DHTermSession {
    deskhubp::TerminalViewer viewer{};
    DHTermCallbacks callbacks{};
};

DHTermSession* dh_term_open(const char* address, const char* passcode, uint16_t cols,
    uint16_t rows, const DHTermCallbacks* callbacks) {
    if (address == nullptr) return nullptr;
    NetAddr server;
    if (!ParseNetAddr(address, server)) return nullptr;

    auto* session = new DHTermSession;
    if (callbacks != nullptr) session->callbacks = *callbacks;

    deskhubp::TerminalViewerConfig config;
    config.host = server;
    config.hostLabel = deskhub::ui::AddressHost(address);
    config.passcode = passcode != nullptr ? passcode : "";
    config.clientName = deskhubp::SessionDeviceName();
    config.size = deskhub::TermSize{cols, rows};

    DHTermSession* raw = session;
    deskhubp::TerminalViewerCallbacks hooks;
    hooks.onState = [raw](deskhubp::TerminalViewerState state, std::string_view message) {
        if (raw->callbacks.onState == nullptr) return;
        const std::string copy(message);
        raw->callbacks.onState(int32_t(state), copy.c_str(), raw->callbacks.user);
    };
    hooks.onRedraw = [raw] {
        if (raw->callbacks.onRedraw != nullptr) raw->callbacks.onRedraw(raw->callbacks.user);
    };
    hooks.onTrustAsked = [raw](deskhub::TrustVerdict verdict, std::string_view fingerprint) {
        if (raw->callbacks.onTrustAsked == nullptr) {
            raw->viewer.RejectFingerprint();
            return;
        }
        const std::string copy(fingerprint);
        raw->callbacks.onTrustAsked(int32_t(verdict), copy.c_str(), raw->callbacks.user);
    };

    if (!session->viewer.Start(config, std::move(hooks))) {
        delete session;
        return nullptr;
    }
    return session;
}

void dh_term_stop(DHTermSession* s) {
    if (s == nullptr) return;
    s->viewer.Stop();
    delete s;
}

int32_t dh_term_state(DHTermSession* s) {
    return s == nullptr ? DHTermIdle : int32_t(s->viewer.State());
}

int dh_term_message(DHTermSession* s, char* out, int capacity) {
    return FillText(out, capacity, s == nullptr ? std::string() : s->viewer.Message());
}

int dh_term_fingerprint(DHTermSession* s, char* out, int capacity) {
    return FillText(out, capacity, s == nullptr ? std::string() : s->viewer.Fingerprint());
}

int32_t dh_term_verdict(DHTermSession* s) {
    return s == nullptr ? int32_t(deskhub::TrustVerdict::Unknown) : int32_t(s->viewer.Verdict());
}

void dh_term_accept_key(DHTermSession* s) {
    if (s != nullptr) s->viewer.AcceptFingerprint();
}

void dh_term_reject_key(DHTermSession* s) {
    if (s != nullptr) s->viewer.RejectFingerprint();
}

bool dh_term_grid(DHTermSession* s, uint32_t scrollOffset, DHTermCell* cells,
    uint32_t cellCapacity, DHTermGrid* outGrid) {
    if (s == nullptr) return false;
    return deskhubp::FillTermGrid(s->viewer.Snapshot(scrollOffset), cells, cellCapacity, outGrid);
}

void dh_term_send_key(DHTermSession* s, int32_t key, uint32_t codepoint, bool shift, bool alt,
    bool ctrl) {
    if (s == nullptr) return;
    deskhub::term::TermKeyEvent event;
    if (!deskhubp::DecodeTermKey(key, codepoint, shift, alt, ctrl, event)) return;
    s->viewer.SendKey(event);
}

void dh_term_send_text(DHTermSession* s, const char* utf8) {
    if (s != nullptr && utf8 != nullptr && *utf8 != '\0') s->viewer.SendText(utf8);
}

void dh_term_paste(DHTermSession* s, const char* utf8) {
    if (s != nullptr && utf8 != nullptr && *utf8 != '\0') s->viewer.Paste(utf8);
}

void dh_term_resize(DHTermSession* s, uint16_t cols, uint16_t rows) {
    if (s != nullptr) s->viewer.Resize(deskhub::TermSize{cols, rows});
}
