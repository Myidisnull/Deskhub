#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "deskhub/media/VideoTypes.h"

namespace deskhub::media {

template <class E, class Frame>
concept VideoEncoderLike = requires {
    static_cast<bool (E::*)(Frame, uint64_t, bool)>(&E::Encode);
    static_cast<bool (E::*)(uint32_t)>(&E::SetBitrate);
    static_cast<void (E::*)()>(&E::Finish);
    static_cast<const char* (E::*)() const>(&E::BackendName);
};

template <class E>
concept HotFpsEncoder = requires { static_cast<bool (E::*)(uint32_t)>(&E::SetFps); };

template <class D>
concept VideoDecoderLike = requires {
    static_cast<bool (D::*)(const uint8_t*, size_t, uint64_t)>(&D::Decode);
};

template <class D, class Surface>
concept SurfaceBoundDecoder = requires {
    static_cast<bool (D::*)(Surface, int, int)>(&D::Init);
};

template <class D>
concept RestartableDecoder = requires {
    static_cast<void (D::*)()>(&D::Shutdown);
    static_cast<bool (D::*)() const>(&D::IsOpen);
};

template <class D>
concept RenderCountingDecoder = requires {
    static_cast<uint32_t (D::*)()>(&D::TakeRenderedCount);
    static_cast<uint64_t (D::*)() const>(&D::lastRenderedPtsUs);
};

template <class D>
concept PresentTimingDecoder = requires {
    static_cast<uint32_t (D::*)()>(&D::TakePresentDelayMs);
    static_cast<uint64_t (D::*)() const>(&D::lastRenderedAtUs);
};

template <class D>
concept CongestionAwareDecoder = requires {
    static_cast<uint32_t (D::*)()>(&D::TakeCongestionDrops);
};

}
