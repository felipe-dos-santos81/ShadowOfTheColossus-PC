// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the periodic-submit decision, kept out of the rasterizer so tests can pin it.
#pragma once

#include <cstdlib>

/// When the command processor hands recorded work to the GPU before a readback asks for it.
///
/// Without this the GPU only gets work at readbacks: it sits idle while the command processor
/// records, then the processor stalls draining it -- the frame is serialised instead of pipelined.
///
/// `SOTC_FLUSH_EVERY=N` submits after N commands (0 turns the periodic submit off); `SOTC_FLUSH_US=N`
/// submits after N microseconds of recording, and an explicit non-zero value also turns the submit
/// on for any GPU (the launcher's option for AMD and Intel cards, not tested there). An explicit
/// timer wins over an explicit count.
///
/// With neither set the game decides. Shadow of the Colossus (CUSA08809) gets the 500 us timer on
/// NVIDIA (measured +20% in the open world) and on KosmicKrisp (measured 14 -> 20-26 fps at 1080p
/// under Rosetta 2); other games keep upstream's no-periodic-submit behaviour. Pink-shadPS4 advises
/// turning it off on AMD (black screen), so AMD stays out.
struct BbPeriodicSubmit {
    /// Submit every this many commands when non-zero.
    unsigned count;
    /// Submit every this many microseconds of recording when non-zero.
    unsigned long long us;
};

inline BbPeriodicSubmit BbPeriodicSubmitFor(const char* flush_every, const char* flush_us,
                                            bool is_sotc, unsigned vendor_id,
                                            bool is_kosmickrisp) {
    constexpr unsigned VendorNvidia = 0x10DE;
    unsigned count = 0;
    if (flush_every != nullptr) {
        count = static_cast<unsigned>(std::strtoul(flush_every, nullptr, 10));
    } else if (flush_us != nullptr && std::strtoull(flush_us, nullptr, 10) != 0) {
        count = 256;
    } else if (is_sotc && (vendor_id == VendorNvidia || is_kosmickrisp)) {
        count = 256;
    }
    unsigned long long us = 0;
    if (flush_us != nullptr) {
        us = std::strtoull(flush_us, nullptr, 10);
    } else if (flush_every == nullptr) {
        us = 500;
    }
    return BbPeriodicSubmit{count, us};
}
