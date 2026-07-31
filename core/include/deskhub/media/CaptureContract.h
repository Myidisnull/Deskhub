#pragma once
#include <concepts>
#include <cstdint>

namespace deskhub::media {

struct FrameMeta {
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t timestampUs = 0;
    uint64_t frameId = 0;
};

struct CaptureOptions {
    uint32_t fps = 60;
    uint32_t maxDim = 0;
};

template <class F>
concept CapturedFrameLike = requires(const F& f) {
    { f.meta } -> std::convertible_to<const FrameMeta&>;
};

template <class C>
concept ScreenCaptureLike = requires {
    static_cast<void (C::*)()>(&C::Stop);
    static_cast<bool (C::*)() const>(&C::Closed);
};

template <class C>
concept QualityAwareCapture = requires {
    static_cast<void (C::*)(uint32_t, uint32_t, uint32_t&, uint32_t&)>(&C::SetQuality);
};

template <class C>
concept ZeroCopyCapture = requires {
    static_cast<bool (C::*)() const>(&C::usingDmaBuf);
};

}
