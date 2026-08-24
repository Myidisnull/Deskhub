#include "deskhub/session/FileTransfer.h"

#include "deskhub/net/TrustStore.h"

namespace deskhub {

std::string TransferAuditLine(const TransferPeer& peer, uint32_t batchId, std::string_view what,
    std::string_view detail) {
    std::string out = "transfer ";
    out += what;
    out += " batch=" + std::to_string(batchId);
    out += " from=" + (peer.endpoint.empty() ? std::string("?") : peer.endpoint);
    out += " name=" + (peer.name.empty() ? std::string("?") : peer.name);
    out += " key=";
    out += IsZero(peer.fingerprint) ? std::string("none") : FormatFingerprint(peer.fingerprint);
    if (!detail.empty()) {
        out += " ";
        out += detail;
    }
    return out;
}

std::string_view TransferReasonName(TransferReason reason) {
    switch (reason) {
        case TransferReason::Accepted: return "accepted";
        case TransferReason::NotAccepting: return "not-accepting";
        case TransferReason::Busy: return "busy";
        case TransferReason::TooManyFiles: return "too-many-files";
        case TransferReason::TooLarge: return "too-large";
        case TransferReason::BadName: return "bad-name";
        case TransferReason::WriteFailed: return "write-failed";
        case TransferReason::Corrupt: return "corrupt";
        case TransferReason::Cancelled: return "cancelled";
        case TransferReason::LinkLost: return "link-lost";
        case TransferReason::ReadFailed: return "read-failed";
    }
    return "unknown";
}

}
