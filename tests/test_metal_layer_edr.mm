// SPDX-License-Identifier: GPL-2.0-or-later
// bbport (macOS): pin the window layer's HDR (EDR) state. A CAMetalLayer on its own is enough --
// no window, no game -- so a wrong color space or a forgotten EDR flag is caught here instead of
// on someone's screen, where it would just look like "HDR changed nothing".
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CAMetalLayer.h>
#include <cassert>
#include <cstdio>

extern "C" void bb_metal_layer_set_hdr(void* layer, int enabled);

static bool has_name(CGColorSpaceRef space, CFStringRef name) {
    CFStringRef actual = space != nullptr ? CGColorSpaceGetName(space) : nullptr;
    return actual != nullptr && CFStringCompare(actual, name, 0) == kCFCompareEqualTo;
}

int main() {
    CAMetalLayer* layer = [CAMetalLayer layer];
    assert(layer != nil);

    // A fresh layer is SDR, which is what the swapchain starts as.
    assert(!layer.wantsExtendedDynamicRangeContent);

    bb_metal_layer_set_hdr((void*)layer, 1);
    assert(layer.wantsExtendedDynamicRangeContent);
    // The swapchain is Rec.2020 + ST2084 (HDR10), so the layer is told PQ, not extended-linear P3.
    assert(has_name(layer.colorspace, kCGColorSpaceITUR_2100_PQ));

    bb_metal_layer_set_hdr((void*)layer, 0);
    assert(!layer.wantsExtendedDynamicRangeContent);
    assert(has_name(layer.colorspace, kCGColorSpaceSRGB));

    // A window with no Metal layer, or a non-Metal platform, must not be a crash.
    bb_metal_layer_set_hdr(nullptr, 1);
    bb_metal_layer_set_hdr(nullptr, 0);

    std::printf("metal-layer-edr: ok\n");
    return 0;
}
