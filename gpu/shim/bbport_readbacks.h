// bbport: the async-readback decision, kept out of BufferCache so tests can pin it.
#pragma once

/// When a guest-thread readback waits for the GPU without keeping the command processor waiting.
///
/// Without it (the synchronous path) the command processor records the readback's copies and then
/// stalls on the GPU: measured ~0.5-0.65 s/s of that stall in the open world. With it, the command
/// processor records and submits the copies and goes on with its queue, the guest thread waits for
/// the GPU, and the data is applied afterwards (a later GPU write to the ranges reads them back
/// again). On macOS/KosmicKrisp this measured +10-18% at 120 Hz vblank (31-37 -> 34-44 fps in the
/// open world); at 60 Hz it holds the 30 fps cap.
///
/// `SOTC_RB_ASYNC=0` turns it off; any other value, or none, keeps the default below.
///
/// Shadow of the Colossus (CUSA08809) gets it by default on KosmicKrisp, where it was measured and
/// soak-tested; other games and other drivers keep the synchronous readbacks. While a readback is
/// in flight a page can be GPU- and CPU-modified at once (write-only protection in the tracker's
/// terms), which PageManager::Protect serves as fully protected, so that is not a crash risk.
inline bool BbAsyncReadbacksFor(const char* env_value, bool is_sotc, bool is_kosmickrisp) {
    if (env_value != nullptr) {
        return env_value[0] != '0';
    }
    return is_sotc && is_kosmickrisp;
}
