#pragma once
#include <algorithm>
#include <cstddef>

namespace deskhub::term {

inline size_t AnchorScroll(size_t scrollOffset, size_t scrollbackWas, size_t scrollbackNow) {
    if (scrollOffset == 0 || scrollbackNow <= scrollbackWas) return scrollOffset;
    return scrollOffset + (scrollbackNow - scrollbackWas);
}

inline size_t ScrollByRows(size_t scrollOffset, int rows, size_t scrollbackRows) {
    const long long delta = rows;
    if (delta > 0)
        scrollOffset += size_t(delta);
    else
        scrollOffset -= std::min(scrollOffset, size_t(-delta));
    return std::min(scrollOffset, scrollbackRows);
}

}
