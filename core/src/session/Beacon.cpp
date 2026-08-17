#include "deskhub/session/Beacon.h"

namespace deskhub {

size_t Beacon::Reply(std::span<uint8_t> out, std::span<const uint8_t> pkt, uint64_t nowUs,
    uint64_t fromPacked) {
    const auto h = ParseCommonHeader(pkt);
    if (!h) return 0;
    const auto payload = PayloadOf(pkt);

    switch (h->type) {
        case MsgType::ListSources: {
            if (fromPacked && authLimit_.Locked(fromPacked, nowUs)) return 0;
            if (!IsValidPasscode(passcode_) || ParseListSourcesPasscode(payload) != passcode_) {
                if (fromPacked) authLimit_.NoteFailure(fromPacked, nowUs);
                return BuildSourceList(out, {});
            }
            if (fromPacked) authLimit_.NoteSuccess(fromPacked);
            return BuildSourceList(out, sources_, caps_);
        }
        case MsgType::Ping: {
            if (h->sessionId != 0) return 0;
            const auto m = ParsePingPong(payload);
            if (!m) return 0;
            return BuildPong(out, 0, *m);
        }
        default:
            return 0;
    }
}

}
