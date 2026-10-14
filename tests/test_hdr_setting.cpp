// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: pin the HDR decision (gpu/shim/bbport_hdr.h): the flag overrides, unset follows the
// display. The bug this guards against shipped for months: HDR was off unless BB_HDR was set, so
// the game's own HDR setting stayed disabled on the XDR panels that can show it.
#include <cassert>
#include <cstdio>
#include "bbport_hdr.h"

int main() {
    // Unset or empty: the display decides, so an XDR panel gets HDR without anyone setting a flag.
    assert(BbHdrAllowed(nullptr, true));
    assert(!BbHdrAllowed(nullptr, false));
    assert(BbHdrAllowed("", true));
    assert(!BbHdrAllowed("", false));

    // Set: it wins either way, so BB_HDR=0 still forces SDR on a display that could show HDR.
    assert(BbHdrAllowed("1", false));
    assert(BbHdrAllowed("yes", false));
    assert(BbHdrAllowed("true", false));
    assert(!BbHdrAllowed("0", true));
    assert(!BbHdrAllowed("no", true));
    assert(!BbHdrAllowed("false", true));

    // Anything that does not start with 1, y or t is off, exactly as Flag() reads the port's other
    // BB_* variables ("on" is not one of them).
    assert(!BbHdrAllowed("on", true));
    assert(!BbHdrAllowed("2", true));

    std::printf("hdr-setting: ok\n");
    return 0;
}
