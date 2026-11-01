/* macos: keeps the Apple GPU at its full clock while the game runs.
 *
 * macOS raises the GPU clock only when the GPU never goes idle. The game's frames leave short
 * gaps (its threads wait for GPU readbacks, then the GPU waits for new work), so the GPU stayed
 * at its lowest clock: M3 Max, gameplay at the Shrine of Worship, 338 MHz and 17.5 FPS (some
 * runs reached ~850 MHz and 23 FPS). One SIMD group kept busy back to back on its own queue holds
 * the top clock (1380 MHz, 25 FPS there) and takes ~1/40 of the GPU; the GPU then draws ~4 W
 * more. PCSX2's Metal renderer does the same ("Spin GPU During Readbacks").
 * By default it runs only on AC power; SOTC_GPU_KEEPALIVE=1 also on battery, =0 never. */
#import <Metal/Metal.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static NSString *const kSpinSource =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "kernel void spin(device float *out [[buffer(0)]], constant uint &n [[buffer(1)]],\n"
     "                 uint i [[thread_position_in_grid]]) {\n"
     "    float x = float(i);\n"
     "    for (uint k = 0; k < n; ++k) x = fma(x, 1.0001f, 0.5f);\n"
     "    out[i] = x;\n"
     "}\n";

static int always; /* SOTC_GPU_KEEPALIVE=1: on battery too */

static int on_ac_power(void) {
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if (!info) return 1;
    CFStringRef source = IOPSGetProvidingPowerSourceType(info);
    const int ac = !source || CFStringCompare(source, CFSTR(kIOPMACPowerKey), 0) == kCFCompareEqualTo;
    CFRelease(info);
    return ac;
}

static void *keepalive(void *arg) {
    (void)arg;
    pthread_setname_np("sotc:GpuKeepAlive");
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        NSError *error = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:kSpinSource options:nil error:&error];
        id<MTLFunction> function = [library newFunctionWithName:@"spin"];
        id<MTLComputePipelineState> pipeline =
            function ? [device newComputePipelineStateWithFunction:function error:&error] : nil;
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLBuffer> out = [device newBufferWithLength:32 * sizeof(float) options:MTLResourceStorageModePrivate];
        if (!pipeline || !queue || !out) {
            fprintf(stderr, "SOTC GPU keep-alive: off (%s)\n",
                    error ? error.localizedDescription.UTF8String : "no Metal device");
            return NULL;
        }
        fprintf(stderr, "SOTC GPU keep-alive: on%s (SOTC_GPU_KEEPALIVE=0 turns it off)\n",
                always ? "" : " while on AC power");
        /* ~15 ms per dispatch at the top clock: the thread wakes ~70 times a second. Two command
         * buffers in flight, so the next one is queued before the last ends: no idle gap. */
        const uint32_t iterations = 1000000;
        id<MTLCommandBuffer> previous = nil;
        for (unsigned round = 0;; ++round) {
            if (!always && round % 64 == 0 && !on_ac_power()) { /* checked about once a second */
                [previous waitUntilCompleted];
                previous = nil;
                while (!on_ac_power()) sleep(2);
            }
            @autoreleasepool {
                id<MTLCommandBuffer> buffer = [queue commandBuffer];
                id<MTLComputeCommandEncoder> encoder = [buffer computeCommandEncoder];
                [encoder setComputePipelineState:pipeline];
                [encoder setBuffer:out offset:0 atIndex:0];
                [encoder setBytes:&iterations length:sizeof(iterations) atIndex:1];
                [encoder dispatchThreads:MTLSizeMake(32, 1, 1) threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
                [encoder endEncoding];
                [buffer commit];
                [previous waitUntilCompleted];
                previous = buffer;
            }
        }
    }
}

void compat_mac_gpu_keepalive(void) {
    const char *setting = getenv("SOTC_GPU_KEEPALIVE");
    if (setting && setting[0] == '0') {
        fprintf(stderr, "SOTC GPU keep-alive: off (SOTC_GPU_KEEPALIVE=0)\n");
        return;
    }
    always = setting && setting[0] == '1';
    pthread_t thread;
    if (pthread_create(&thread, NULL, keepalive, NULL) == 0) pthread_detach(thread);
}
