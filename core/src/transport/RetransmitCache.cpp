#include "deskhub/transport/RetransmitCache.h"

#include <algorithm>

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
    if (datagram.size() > kMaxDatagram) return;
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
        slot->lengths.assign(v->hdr.pktCount, 0);
        slot->bytes.resize(size_t(v->hdr.pktCount) * kMaxDatagram);
    } else if (slot->lengths.size() < v->hdr.pktCount) {
        slot->lengths.resize(v->hdr.pktCount, 0);
        slot->bytes.resize(size_t(v->hdr.pktCount) * kMaxDatagram);
    }

    if (v->hdr.pktIndex >= slot->lengths.size()) return;
    std::copy(datagram.begin(), datagram.end(),
        slot->bytes.begin() + ptrdiff_t(size_t(v->hdr.pktIndex) * kMaxDatagram));
    slot->lengths[v->hdr.pktIndex] = uint16_t(datagram.size());
}

std::span<const uint8_t> RetransmitCache::Find(uint32_t frameId, uint16_t pktIndex) const {
    const FrameSlot* slot = FindSlot(frameId);
    if (!slot || pktIndex >= slot->lengths.size()) return {};
    const uint16_t length = slot->lengths[pktIndex];
    if (length == 0) return {};
    return std::span<const uint8_t>(slot->bytes.data() + size_t(pktIndex) * kMaxDatagram, length);
}

void RetransmitCache::Reset() {
    for (FrameSlot& s : slots_) {
        s.used = false;
        s.frameId = 0;
        s.lengths.clear();
    }
    next_ = 0;
}

}
