// SPDX-License-Identifier: GPL-2.0-or-later
// bbport (macOS): whether the window's CAMetalLayer is carrying HDR (EDR) content.
//
// SDL3 exposes display and window HDR as read-only properties, and SDL_Metal_CreateView hands the
// CAMetalLayer to the caller, so this is the port's to set. Without the EDR flag and a matching
// color space a 10-bit ST2084 swapchain is composited as if it were SDR -- dark and washed out --
// so the game's own HDR setting would change nothing on screen.
#include <CoreGraphics/CoreGraphics.h>
#include <QuartzCore/CAMetalLayer.h>

extern "C" void bb_metal_layer_set_hdr(void* layer, int enabled) {
    CAMetalLayer* metal_layer = (CAMetalLayer*)layer;
    if (metal_layer == nil) {
        return;
    }
    const bool hdr = enabled != 0;
    metal_layer.wantsExtendedDynamicRangeContent = hdr ? YES : NO;
    // The swapchain is Rec.2020 with ST2084 (HDR10), so the layer is told PQ. Extended-linear P3
    // would read those same numbers as a different encoding and shift the whole picture.
    CGColorSpaceRef space =
        CGColorSpaceCreateWithName(hdr ? kCGColorSpaceITUR_2100_PQ : kCGColorSpaceSRGB);
    if (space != nullptr) {
        metal_layer.colorspace = space;
        CGColorSpaceRelease(space);
    }
}
