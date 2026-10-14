// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: pin the windowed-mode size clamp (gpu/shim/window_scale.h) to its behaviour.
//
// The clamp is why the acceptance gate must not fix a window size: a display smaller than the
// requested one scales it down, and the marker reports whatever SDL was given.
#include <cassert>
#include <cstdio>
#include "window_scale.h"

using Frontend::ScaleWindowToDisplay;
using Frontend::WindowSize;

static bool operator==(WindowSize a, WindowSize b) {
    return a.width == b.width && a.height == b.height;
}

int main() {
    // A request that fits is not touched.
    assert((ScaleWindowToDisplay({1280, 720}, {1920, 1080}) == WindowSize{1280, 720}));

    // A request larger than the display: both axes scaled by the same factor, to 90% of the
    // usable area (4K request, 1080p display).
    assert((ScaleWindowToDisplay({3840, 2160}, {1920, 1080}) == WindowSize{1728, 972}));

    // The factor is the stricter of the two axes (an ultrawide request on a 16:9 display), and
    // the aspect ratio is kept: 2560x1080 * 0.675 == 1728x729.
    assert((ScaleWindowToDisplay({2560, 1080}, {1920, 1080}) == WindowSize{1728, 729}));

    // Exactly 90% fits: the comparison is `>`, so a window at the boundary stays put.
    assert((ScaleWindowToDisplay({1728, 972}, {1920, 1080}) == WindowSize{1728, 972}));

    // A 1080p request on a display with no room for it: the size the gate's scaled-window test
    // uses (1280x720 request, 1280x720 usable -> 1152x648).
    assert((ScaleWindowToDisplay({1280, 720}, {1280, 720}) == WindowSize{1152, 648}));

    // Aspect ratio preserved, and the result fits the usable area.
    const WindowSize wide = ScaleWindowToDisplay({1600, 900}, {1280, 1024});
    assert((wide == WindowSize{1152, 648}));
    assert(wide.width * 900 == wide.height * 1600);

    // No display data: the request is returned unchanged rather than scaled to nothing.
    assert((ScaleWindowToDisplay({1280, 720}, {0, 0}) == WindowSize{1280, 720}));
    assert((ScaleWindowToDisplay({1280, 720}, {-1, 1080}) == WindowSize{1280, 720}));

    // A huge request on a tiny display still comes back positive.
    const WindowSize tiny = ScaleWindowToDisplay({10000, 10000}, {100, 100});
    assert(tiny.width > 0 && tiny.height > 0);

    std::printf("window-scale: ok\n");
    return 0;
}
