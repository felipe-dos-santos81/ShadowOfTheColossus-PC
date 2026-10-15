// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: pin the async-readback decision (gpu/shim/bbport_readbacks.h). Without it the command
// processor waits for the GPU at every guest readback: ~0.5-0.65 s/s of stall in the open world
// on macOS/KosmicKrisp, where the asynchronous path measured 31-37 -> 34-44 fps (120 Hz vblank).
#include <cassert>
#include <cstdio>
#include "bbport_readbacks.h"

int main() {
    // Unset: Shadow of the Colossus gets the asynchronous readbacks on KosmicKrisp, where they
    // were measured and soak-tested. Other games and other drivers keep the synchronous path.
    assert(BbAsyncReadbacksFor(nullptr, true, true));
    assert(!BbAsyncReadbacksFor(nullptr, true, false));
    assert(!BbAsyncReadbacksFor(nullptr, false, true));

    // SOTC_RB_ASYNC overrides the default both ways; anything but a leading 0 enables.
    assert(!BbAsyncReadbacksFor("0", true, true));
    assert(BbAsyncReadbacksFor("1", false, false));
    assert(BbAsyncReadbacksFor("2", true, false));
    assert(!BbAsyncReadbacksFor("0", false, false));

    std::printf("async-readbacks: ok\n");
    return 0;
}
