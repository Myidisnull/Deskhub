#include "deskhub/transport/RetransmitCache.h"

namespace deskhub {

RetransmitCache::FrameSlot* RetransmitCache::FindSlot(uint32_t frameId) {
    for (FrameSlot& s : slots_)
        if (s.used && s.frameId == frameId) return &s;
    return nullptr;
}

const RetransmitCache::FrameSlot* RetransmitCache::FindSlot(uint32_t frameId) const {
    for (const FrameSlot& s : slots_)
        if (s.used && s.frameId == frameId) return &s;
    return nullptr;
}

void RetransmitCache::Store(std::span<const uint8_t> datagram) {
    const auto h = ParseCommonHeader(datagram);
    if (!h || h->type != MsgType::VideoPacket) return;
    const auto v = ParseVideoPacket(*h, PayloadOf(datagram));
    if (!v) return;

    FrameSlot* slot = FindSlot(v->hdr.frameId);
    if (!slot) {
        slot = &slots_[next_];
        next_ = (next_ + 1) % kCacheFrames;
        slot->frameId = v->hdr.frameId;
        slot->used = true;
        slot->packets.assign(v->hdr.pktCount, {});
    } else if (slot->packets.size() < v->hdr.pktCount) {
        slot->packets.resize(v->hdr.pktCount);
    }

    if (v->hdr.pktIndex < slot->packets.size())
        slot->packets[v->hdr.pktIndex].assign(datagram.begin(), datagram.end());
}

std::span<const uint8_t> RetransmitCache::Find(uint32_t frameId, uint16_t pktIndex) const {
    const FrameSlot* slot = FindSlot(frameId);
    if (!slot || pktIndex >= slot->packets.size()) return {};
    const std::vector<uint8_t>& d = slot->packets[pktIndex];
    return std::span<const uint8_t>(d.data(), d.size());
}

void RetransmitCache::Reset() {
    for (FrameSlot& s : slots_) {
        s.used = false;
        s.frameId = 0;
        s.packets.clear();
    }
    next_ = 0;
}

}
