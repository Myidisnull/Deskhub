#pragma once
#include <string>
#include <string_view>

#include "deskhub/input/VirtualKeys.h"
#include "deskhub/media/ViewerTitle.h"

namespace deskhub {

inline constexpr int32_t kViewerLockToggleVk = kVkF1 + 8;

struct PointerLockEffect {
    bool lockChanged = false;
    bool releaseHeldInput = false;
};

class PointerLockState {
public:
    PointerLockState() = default;
    explicit PointerLockState(bool locked) : locked_(locked) {}

    bool locked() const {
        return locked_;
    }

    PointerLockEffect OnToggleLockKey() {
        locked_ = !locked_;
        return PointerLockEffect{true, false};
    }

    PointerLockEffect OnEscape() {
        if (!locked_) return PointerLockEffect{};
        locked_ = false;
        return PointerLockEffect{true, false};
    }

    PointerLockEffect OnFocusLost() {
        PointerLockEffect e;
        e.releaseHeldInput = true;
        if (locked_) {
            locked_ = false;
            e.lockChanged = true;
        }
        return e;
    }

    std::string_view HintText() const {
        return ViewerLockHintText(locked_);
    }

    std::string SubtitleFor(std::string_view statusLine) const {
        return ViewerStatusWithHint(statusLine, locked_);
    }

    std::string TitleFor(std::string_view baseTitle, std::string_view statusLine) const {
        return ComposeViewerTitle(baseTitle, statusLine, HintText());
    }

private:
    bool locked_ = false;
};

}
