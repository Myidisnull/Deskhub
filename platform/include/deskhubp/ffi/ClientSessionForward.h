#pragma once
#include "deskhub/input/Hotkeys.h"
#include "deskhub/session/LinkPulse.h"
#include "deskhub/session/client/FileSender.h"
#include "deskhubp/ffi/ClientSession.h"
#include "deskhubp/ffi/FfiText.h"

#include <cstring>
#include <filesystem>
#include <vector>

#define DESKHUB_DEFINE_CLIENT_SESSION_FORWARDERS(engineOf)                                 \
    void dh_session_key(DHSession* s, int32_t vk, int32_t scan, bool down) {               \
        if (s) engineOf(s).QueueKey(vk, scan, down);                                       \
    }                                                                                      \
                                                                                           \
    void dh_session_hotkey(DHSession* s, int32_t vk, int32_t scan, int32_t modVk,          \
        int32_t modScan) {                                                                 \
        if (!s) return;                                                                    \
        deskhub::DispatchHotkey(engineOf(s),                                               \
            deskhub::Hotkey{"", vk, scan, modVk, modScan});                                \
    }                                                                                      \
                                                                                           \
    void dh_session_char_tap(DHSession* s, uint32_t codepoint) {                           \
        if (s) engineOf(s).QueueCharTap(codepoint);                                        \
    }                                                                                      \
                                                                                           \
    void dh_session_release_all_input(DHSession* s) {                                      \
        if (s) engineOf(s).ReleaseAllInput();                                              \
    }                                                                                      \
                                                                                           \
    void dh_session_mouse_move(DHSession* s, int32_t nx, int32_t ny) {                     \
        if (s) engineOf(s).QueueMouseMoveAbs(nx, ny);                                      \
    }                                                                                      \
                                                                                           \
    void dh_session_mouse_move_rel(DHSession* s, int32_t dx, int32_t dy) {                 \
        if (s) engineOf(s).QueueMouseMoveRel(dx, dy);                                      \
    }                                                                                      \
                                                                                           \
    void dh_session_mouse_button(DHSession* s, int32_t button, bool down) {                \
        if (s) engineOf(s).QueueMouseButton(button, down);                                 \
    }                                                                                      \
                                                                                           \
    void dh_session_mouse_wheel(DHSession* s, int32_t delta) {                             \
        if (s) engineOf(s).QueueMouseWheel(delta);                                         \
    }                                                                                      \
                                                                                           \
    void dh_session_mouse_wheel_notches(DHSession* s, int32_t notches) {                   \
        if (s) engineOf(s).QueueMouseWheel(notches* deskhub::kWheelDeltaPerNotch);         \
    }                                                                                      \
                                                                                           \
    void dh_session_snapshot(DHSession* s, DHSessionState* out) {                          \
        if (!out) return;                                                                  \
        *out = DHSessionState{};                                                           \
        if (!s) return;                                                                    \
        out->phase = DHPhase(int(engineOf(s).phase()));                                    \
        out->videoWidth = engineOf(s).videoWidth();                                        \
        out->videoHeight = engineOf(s).videoHeight();                                      \
        deskhubp::CopyToBuf(out->statusLine, sizeof(out->statusLine),                      \
            engineOf(s).StatusLine());                                                     \
        deskhubp::CopyToBuf(out->endReason, sizeof(out->endReason),                        \
            engineOf(s).EndReason());                                                      \
    }                                                                                      \
                                                                                           \
    void dh_session_link_health(DHSession* s, DHLinkHealth* out) {                         \
        if (!out) return;                                                                  \
        *out = DHLinkHealth{};                                                             \
        if (!s) return;                                                                    \
        const deskhub::LinkPulseView view = engineOf(s).LinkHealth();                      \
        out->haveRtt = view.haveRtt;                                                       \
        out->rttMs = (view.rttUs + 500) / 1000;                                            \
        out->lossPct = view.lossPct;                                                       \
        out->quality = DHLinkQuality(int(view.quality));                                   \
    }                                                                                      \
                                                                                           \
    int dh_session_file_send(DHSession* s, const char* const* paths, int count) {          \
        if (!s || !paths || count <= 0) return 0;                                          \
        std::vector<std::filesystem::path> list;                                           \
        list.reserve(size_t(count));                                                         \
        for (int i = 0; i < count; ++i) {                                                  \
            if (!paths[i] || !*paths[i]) return 0;                                         \
            list.emplace_back(paths[i]);                                                   \
        }                                                                                  \
        return engineOf(s).BeginFileSend(list) ? 1 : 0;                                      \
    }                                                                                      \
                                                                                           \
    void dh_session_file_cancel(DHSession* s) {                                            \
        if (s) engineOf(s).CancelFileSend();                                               \
    }                                                                                      \
                                                                                           \
    int dh_session_file_busy(DHSession* s) {                                               \
        return s && engineOf(s).FileSendBusy() ? 1 : 0;                                      \
    }                                                                                      \
                                                                                           \
    void dh_session_file_progress(DHSession* s, DHFileSendProgress* out) {                 \
        if (!out) return;                                                                    \
        *out = DHFileSendProgress{};                                                       \
        if (!s) return;                                                                    \
        const deskhub::TransferProgress p = engineOf(s).FileSendProgress();                \
        const deskhub::FileSenderState st = engineOf(s).FileSendState();                     \
        out->phase = DHFileSendPhase(int(st));                                               \
        out->batchId = p.batchId;                                                            \
        out->fileIndex = p.fileIndex;                                                        \
        out->fileCount = p.fileCount;                                                        \
        out->fileBytes = p.fileBytes;                                                        \
        out->fileSize = p.fileSize;                                                          \
        out->batchBytes = p.batchBytes;                                                      \
        out->batchSize = p.batchSize;                                                        \
        deskhubp::CopyToBuf(out->name, sizeof(out->name), p.name);                         \
    }                                                                                      \
                                                                                           \
    const char* dh_session_file_error(DHSession* s) {                                      \
        if (!s) return "";                                                                   \
        deskhubp::CopyToBuf(s->statusBuf, sizeof(s->statusBuf),                            \
            engineOf(s).FileSendError());                                                  \
        return s->statusBuf;                                                                 \
    }                                                                                      \
                                                                                           \
    DHPhase dh_session_phase(DHSession* s) {                                               \
        if (!s) return DHPhaseIdle;                                                        \
        return DHPhase(int(engineOf(s).phase()));                                          \
    }                                                                                      \
                                                                                           \
    const char* dh_session_status_line(DHSession* s) {                                     \
        if (!s) return "";                                                                 \
        deskhubp::CopyToBuf(s->statusBuf, sizeof(s->statusBuf), engineOf(s).StatusLine()); \
        return s->statusBuf;                                                               \
    }                                                                                      \
                                                                                           \
    const char* dh_session_end_reason(DHSession* s) {                                      \
        if (!s) return "";                                                                 \
        deskhubp::CopyToBuf(s->reasonBuf, sizeof(s->reasonBuf), engineOf(s).EndReason());  \
        return s->reasonBuf;                                                               \
    }                                                                                      \
                                                                                           \
    uint32_t dh_session_video_width(DHSession* s) {                                        \
        return s ? engineOf(s).videoWidth() : 0;                                           \
    }                                                                                      \
                                                                                           \
    uint32_t dh_session_video_height(DHSession* s) {                                       \
        return s ? engineOf(s).videoHeight() : 0;                                          \
    }                                                                                      \
                                                                                           \
    void dh_session_clip_offer(DHSession* s, const char* text) {                           \
        if (s && text && *text) engineOf(s).OfferLocalClipboard(text);                     \
    }                                                                                      \
                                                                                           \
    int dh_session_clip_take(DHSession* s, char* out, int capacity) {                      \
        if (!out || capacity <= 0) return 0;                                               \
        out[0] = '\0';                                                                     \
        if (!s) return 0;                                                                  \
        const auto text = engineOf(s).TakeRemoteClipboard();                               \
        if (!text) return 0;                                                               \
        deskhubp::CopyToBuf(out, size_t(capacity), *text);                                 \
        return int(std::strlen(out));                                                      \
    }
