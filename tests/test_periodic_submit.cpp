// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: pin the periodic-submit decision (gpu/shim/bbport_flush.h). Without it the frame is
// serialised: the GPU idles while the command processor records, then the processor stalls draining
// it at readbacks. On this port's macOS driver that was ~1.8x of frame time (14 -> 26 fps).
#include <cassert>
#include <cstdio>
#include "bbport_flush.h"

static void expect(BbPeriodicSubmit got, unsigned count, unsigned long long us) {
    assert(got.count == count);
    assert(got.us == us);
}

int main() {
    constexpr unsigned kNvidia = 0x10DE;
    constexpr unsigned kOther = 0x1002; // AMD: the vendor whose cards the periodic submit hurt.

    // Unset: Shadow of the Colossus gets the 500 us timer on NVIDIA (where it was first measured)
    // and on KosmicKrisp (this port's macOS driver). Every other game, and every other GPU, keeps
    // upstream's behaviour: no periodic submit.
    expect(BbPeriodicSubmitFor(nullptr, nullptr, true, kNvidia, false), 256, 500);
    expect(BbPeriodicSubmitFor(nullptr, nullptr, true, kOther, true), 256, 500);
    expect(BbPeriodicSubmitFor(nullptr, nullptr, true, kOther, false), 0, 500);
    expect(BbPeriodicSubmitFor(nullptr, nullptr, false, kNvidia, true), 0, 500);

    // An explicit count wins over the 500 us default and leaves the timer off, so the two cannot
    // fight over which submits.
    expect(BbPeriodicSubmitFor("64", nullptr, true, kOther, true), 64, 0);
    expect(BbPeriodicSubmitFor("0", nullptr, true, kNvidia, false), 0, 0);

    // An explicit timer value is used as given, and a non-zero one turns the submit on for any GPU.
    expect(BbPeriodicSubmitFor(nullptr, "1000", true, kOther, false), 256, 1000);
    expect(BbPeriodicSubmitFor(nullptr, "0", false, kOther, false), 0, 0);
    expect(BbPeriodicSubmitFor(nullptr, "0", true, kNvidia, false), 256, 0);

    // An explicit timer wins over an explicit count, exactly as it did when the two were separate.
    expect(BbPeriodicSubmitFor("64", "1000", true, kNvidia, false), 64, 1000);

    std::printf("periodic-submit: ok\n");
    return 0;
}
