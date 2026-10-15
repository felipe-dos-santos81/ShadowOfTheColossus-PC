// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: counters printed with the frame rate (SOTC_FPS=1, videoout/driver.cpp).
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace SotcStats {
inline std::atomic<uint64_t> readbacks{0};    ///< BufferCache::DownloadMemory calls
inline std::atomic<uint64_t> readback_ns{0};  ///< time spent in them (copy and wait)
inline std::atomic<uint64_t> watch_addr{0};     ///< SOTC_CS_WATCH: GPU writes to report
inline std::atomic<uint64_t> current_program{0}; ///< hash of the dispatch being recorded (1 = a draw)
inline std::atomic<uint64_t> readback_ahead{0};   ///< downloads of already submitted work
inline std::atomic<uint64_t> readback_finish{0};  ///< downloads that flushed the current work
inline std::atomic<uint64_t> heap_guard_builds{0}; ///< readbacks into the BPE heap
inline std::atomic<uint64_t> readback_copies{0};          ///< ranges downloaded
inline std::atomic<uint64_t> readback_copies_changed{0};  ///< ... whose bytes differed from memory
inline std::atomic<uint64_t> readback_bytes{0};
inline std::atomic<uint64_t> readback_bytes_changed{0};
inline std::atomic<uint64_t> uploads{0};        ///< SynchronizeMemory copies (CPU data to GPU)
inline std::atomic<uint64_t> upload_bytes{0};
inline std::atomic<uint64_t> render_breaks{0};  ///< EndRendering that closed a render pass
inline std::atomic<uint64_t> fault_writes{0};       ///< guest write faults on tracked pages
inline std::atomic<uint64_t> fault_walker{0};       ///< SRT walker loads emulated after a fault
inline std::atomic<uint64_t> fault_gpu_reads{0};    ///< other read faults on the command processor
inline std::atomic<uint64_t> commands{0}; ///< draws and dispatches recorded
/// SOTC_CMD_STATS=1: draws, indirect draws (arguments GPU-written / CPU-side / CPU-side and
/// empty), dispatches, indirect dispatches.
inline std::atomic<uint64_t> cmd_kinds[6]{};
inline std::atomic<uint64_t> early_flushes{0}; ///< submits right after a write to a read-back page
/// SOTC_THREAD_STATS=1: where each rendering thread's time goes, printed with the frame rate.
inline std::atomic<uint64_t> cp_busy_ns{0}; ///< command processor recording Draw/Dispatch calls
inline std::atomic<uint64_t> cp_cmds{0};    ///< ... commands timed (sampled 1 in N)
inline std::atomic<uint64_t> replay_busy_ns{0};  ///< recorder replay thread executing commands
inline std::atomic<uint64_t> replay_cmds{0};     ///< ... commands executed
inline std::atomic<uint64_t> replay_spin_ns{0};  ///< ... spinning for work
inline std::atomic<uint64_t> replay_sleep_ns{0}; ///< ... sleeping for work
inline std::atomic<uint64_t> flushes{0};         ///< queue submits
inline std::atomic<uint64_t> flush_ns{0};        ///< ... time spent in Scheduler::SubmitExecution
inline std::atomic<uint64_t> recorder_backpressure{0}; ///< producer waited for the replay thread

struct ScopedTimer {
    std::atomic<uint64_t>& total;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    explicit ScopedTimer(std::atomic<uint64_t>& t) : total{t} {}
    ~ScopedTimer() {
        total.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - start)
                            .count(),
                        std::memory_order_relaxed);
    }
};

/// SOTC_THREAD_STATS=1 turns the per-thread counters above on; read once.
inline bool ThreadStatsEnabled() {
    static const bool value = std::getenv("SOTC_THREAD_STATS") != nullptr;
    return value;
}

/// SOTC_BUSY_EVERY=N times only 1 in N command-processor calls (default 16): two clock reads per
/// command would cost more than the measurement. The print scales the sampled totals by this.
inline uint64_t BusySampleEvery() {
    static const uint64_t value = [] {
        const char* v = std::getenv("SOTC_BUSY_EVERY");
        const long n = v ? std::strtol(v, nullptr, 10) : 16;
        return static_cast<uint64_t>(n < 1 ? 1 : n);
    }();
    return value;
}

/// Times one sampled command-processor call (see BusySampleEvery); does nothing when disabled.
struct CpCommandTimer {
    bool active = false;
    std::chrono::steady_clock::time_point start{};

    CpCommandTimer() {
        if (!ThreadStatsEnabled()) {
            return;
        }
        static thread_local uint64_t calls = 0;
        active = (++calls % BusySampleEvery()) == 0;
        if (active) {
            start = std::chrono::steady_clock::now();
        }
    }
    ~CpCommandTimer() {
        if (!active) {
            return;
        }
        cp_busy_ns.fetch_add(
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now() - start)
                                      .count()),
            std::memory_order_relaxed);
        cp_cmds.fetch_add(1, std::memory_order_relaxed);
    }
};

/// SOTC_DRAW_STATS=1: the sampled draw's own breakdown, per sampled draw, every five seconds
/// (key: pipeline key refresh and lookup; bind: render state and descriptor preparation; state:
/// vertex buffers, resource binds and dynamic state; emit: the draw call itself). The sample is
/// 1 in SOTC_BUSY_EVERY.
inline std::atomic<uint64_t> draw_key_ns{0};
inline std::atomic<uint64_t> draw_bind_ns{0};
inline std::atomic<uint64_t> draw_state_ns{0};
inline std::atomic<uint64_t> draw_emit_ns{0};
inline std::atomic<uint64_t> draw_stat_cmds{0};

inline bool DrawStatsEnabled() {
    static const bool value = std::getenv("SOTC_DRAW_STATS") != nullptr;
    return value;
}

/// SOTC_DRAW_STATS=1: how often a graphics draw repeats the previous draw's pipeline key. That is
/// the optimistic bound for a fast path that reuses the previous draw's pipeline and bindings.
inline std::atomic<uint64_t> draw_key_total{0};
inline std::atomic<uint64_t> draw_key_repeats{0};

/// sotc: whether the draw being recorded is the sampled one (set by the draw paths, read by the
/// pipeline cache and the resource binding code to time their sub-phases).
inline thread_local bool draw_sample = false;

/// Sub-phases of the sampled draw: shader stages/permutations, pipeline map lookup, render state
/// preparation, buffer descriptor building and texture descriptor building.
inline std::atomic<uint64_t> draw_stages_ns{0};
inline std::atomic<uint64_t> draw_lookup_ns{0};
inline std::atomic<uint64_t> draw_prepare_ns{0};
inline std::atomic<uint64_t> draw_bufbind_ns{0};
inline std::atomic<uint64_t> draw_texbind_ns{0};
inline std::atomic<uint64_t> draw_getprogram_ns{0};
inline std::atomic<uint64_t> draw_flatbuf_ns{0};

/// Parts of the flat-buffer refresh: the generated SRT walker and the dynamic T# tables read from
/// guest memory.
inline std::atomic<uint64_t> draw_flatbuf_walker_ns{0};
inline std::atomic<uint64_t> draw_flatbuf_tsharp_ns{0};

/// True for the draws that SOTC_DRAW_STATS samples (1 in SOTC_BUSY_EVERY).
inline bool SampleDraw() {
    static thread_local uint64_t calls = 0;
    return (++calls % BusySampleEvery()) == 0;
}

/// Times one draw phase between construction and destruction; inert when not sampled.
struct DrawPhaseTimer {
    std::chrono::steady_clock::time_point start;
    std::atomic<uint64_t>* counter{};

    DrawPhaseTimer(std::atomic<uint64_t>& c, bool active) : counter(active ? &c : nullptr) {
        if (counter) {
            start = std::chrono::steady_clock::now();
        }
    }
    ~DrawPhaseTimer() {
        if (!counter) {
            return;
        }
        counter->fetch_add(
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now() - start)
                                      .count()),
            std::memory_order_relaxed);
    }
};

/// SOTC_DROP_TRACE=1: logs the first 80 draws/dispatches of each kind that are dropped.
inline void Drop(int kind, unsigned long long a, unsigned long long b) {
    static const bool enabled = [] {
        const char* v = std::getenv("SOTC_DROP_TRACE");
        return v && v[0] == '1';
    }();
    if (!enabled) {
        return;
    }
    static std::atomic<int> shown[8]{};
    static const char* names[8] = {"draw-nopipeline", "draw-nobind", "dispatch-nopipeline",
                                   "dispatch-nobind", "condexec-skip", "dispatch-hle", "?", "?"};
    if (shown[kind & 7].fetch_add(1) < 80) {
        std::fprintf(stderr, "SOTC drop %s %#llx %#llx\n", names[kind & 7], a, b);
    }
}
} // namespace SotcStats
