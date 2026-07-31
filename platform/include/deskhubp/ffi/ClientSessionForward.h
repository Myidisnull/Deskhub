#pragma once
#include "deskhubp/ffi/ClientSession.h"
#include "deskhubp/ffi/FfiText.h"

#define DESKHUB_DEFINE_CLIENT_SESSION_FORWARDERS(engineOf)                                 \
    void dh_session_key(DHSession* s, int32_t vk, int32_t scan, bool down) {               \
        if (s) engineOf(s).QueueKey(vk, scan, down);                                       \
    }                                                                                      \
                                                                                           \
    void dh_session_key_tap(DHSession* s, int32_t vk, int32_t scan) {                      \
        if (s) engineOf(s).QueueKeyTap(vk, scan);                                          \
    }                                                                                      \
                                                                                           \
    void dh_session_key_chord(DHSession* s, int32_t modVk, int32_t modScan, int32_t vk,    \
        int32_t scan) {                                                                    \
        if (s) engineOf(s).QueueKeyChord(modVk, modScan, vk, scan);                        \
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
    }
