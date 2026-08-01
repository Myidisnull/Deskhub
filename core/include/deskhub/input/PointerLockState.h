#pragma once
#include <string>
#include <string_view>

#include "deskhub/media/ViewerTitle.h"

namespace deskhub {

inline constexpr std::string_view kViewerPauseHint = "input paused (F10)";

struct PointerLockEffect {
    bool lockChanged = false;
    bool pauseChanged = false;
    bool releaseHeldInput = false;

    bool anyChange() const {
        return lockChanged || pauseChanged;
    }
};

class PointerLockState {
public:
    PointerLockState() = default;
    PointerLockState(bool locked, bool paused) : locked_(locked), paused_(paused) {}

    bool locked() const {
        return locked_;
    }

    bool paused() const {
        return paused_;
    }

    bool acceptsInput() const {
        return !paused_;
    }

    PointerLockEffect OnToggleLockKey() {
        locked_ = !locked_;
        return PointerLockEffect{true, false, false};
    }

    PointerLockEffect OnTogglePauseKey() {
        paused_ = !paused_;
        return PointerLockEffect{false, true, paused_};
    }

    PointerLockEffect OnEscape() {
        if (!locked_) return PointerLockEffect{};
        locked_ = false;
        return PointerLockEffect{true, false, false};
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
        std::string s = ViewerStatusWithHint(statusLine, locked_);
        if (paused_) {
            s += " \xC2\xB7 ";
            s += kViewerPauseHint;
        }
        return s;
    }

    std::string TitleFor(std::string_view baseTitle, std::string_view statusLine) const {
        std::string title = ComposeViewerTitle(baseTitle, statusLine, HintText());
        if (paused_) {
            title += " \xC2\xB7 ";
            title += kViewerPauseHint;
        }
        return title;
    }

private:
    bool locked_ = false;
    bool paused_ = false;
};

}
