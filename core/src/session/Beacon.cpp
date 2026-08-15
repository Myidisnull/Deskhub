#include "deskhub/session/Beacon.h"

namespace deskhub {

size_t Beacon::Reply(std::span<uint8_t> out, std::span<const uint8_t> pkt, bool trusted) const {
    const auto h = ParseCommonHeader(pkt);
    if (!h) return 0;
    const auto payload = PayloadOf(pkt);

    switch (h->type) {
        // What is shared is only told to a machine that has already proved itself on an
        // encrypted connection. A stranger gets an empty list whatever passcode it
        // offers, so probing the beacon can no longer tell a right guess from a wrong
        // one - that difference used to be a free oracle for guessing the code.
        case MsgType::ListSources: {
            if (!trusted) return BuildSourceList(out, {});
            return BuildSourceList(out, sources_);
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
