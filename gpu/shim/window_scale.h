// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the windowed-mode size clamp, kept out of window.cpp so tests can pin it.
#pragma once
#include <algorithm>

namespace Frontend {

struct WindowSize {
    int width;
    int height;
};

/// The size to open the window at, in windowed mode.
///
/// The requested size is kept unless it does not fit the display's usable area, in which case both
/// axes are scaled by the same factor until the window fits within 90% of it (the aspect ratio is
/// kept). `usable` is the display's usable area; a non-positive one -- no display data -- returns
/// the request unchanged. The 90% leaves room for the title bar and the Dock, and the integer
/// comparison is what decides "does not fit": a request that is 90%-or-less is untouched.
inline WindowSize ScaleWindowToDisplay(WindowSize requested, WindowSize usable) {
    if (usable.width <= 0 || usable.height <= 0) {
        return requested;
    }
    const bool too_wide = requested.width > usable.width * 9 / 10;
    const bool too_tall = requested.height > usable.height * 9 / 10;
    if (!too_wide && !too_tall) {
        return requested;
    }
    const double scale = std::min(usable.width * 0.9 / requested.width,
                                  usable.height * 0.9 / requested.height);
    // Rounded, not truncated: `requested * 0.9` can land just under the integer (1152 comes back
    // as 1151.999...), and a window one pixel smaller than asked for is not the point of the clamp.
    return {static_cast<int>(requested.width * scale + 0.5),
            static_cast<int>(requested.height * scale + 0.5)};
}

} // namespace Frontend
