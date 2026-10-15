// SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <chrono>
#include <cstdio>
#include <cstdlib>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/thread_info.h>
#endif
#include "common/assert.h"
#include "common/debug.h"
#include "common/thread.h"
#include "core/debug_state.h"
#include "core/emulator_settings.h"
#include "core/libraries/kernel/time.h"
#include "core/libraries/videoout/driver.h"
#include "core/libraries/videoout/videoout_error.h"
#include "imgui/renderer/imgui_core.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/renderer_vulkan/vk_presenter.h"
#include "video_core/sotc_stats.h"

extern std::unique_ptr<Vulkan::Presenter> presenter;
extern std::unique_ptr<AmdGpu::Liverpool> liverpool;

namespace {
// sotc: what the present thread does between frame-rate lines (SOTC_PRESENT_STATS=1 prints them
// next to the frame rate). `catchup` counts iterations the pacing timer would not let sleep
// because the previous ones ran late, and `min_wait` is how deep that lateness got.
struct PresentStats {
    u64 iterations = 0;
    u64 presents = 0;
    u64 present_ns = 0;
    u64 max_present_ns = 0;
    u64 catchup = 0;
    s64 min_wait_ns = 0;
    u64 cpu_ns = 0;
};
PresentStats present_stats;
bool present_realtime = true;

/// CPU time of the calling thread in nanoseconds (macOS); zero elsewhere.
u64 PresentThreadCpuNs() {
#ifdef __APPLE__
    const thread_t thread = mach_thread_self();
    thread_basic_info_data_t info{};
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    const kern_return_t result =
        thread_info(thread, THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&info), &count);
    mach_port_deallocate(mach_task_self(), thread);
    if (result != KERN_SUCCESS) {
        return 0;
    }
    return static_cast<u64>(info.user_time.seconds + info.system_time.seconds) * 1000000000ull +
           static_cast<u64>(info.user_time.microseconds + info.system_time.microseconds) * 1000ull;
#else
    return 0;
#endif
}
} // namespace

namespace Libraries::VideoOut {

constexpr static bool Is32BppPixelFormat(PixelFormat format) {
    switch (format) {
    case PixelFormat::A8R8G8B8Srgb:
    case PixelFormat::A8B8G8R8Srgb:
    case PixelFormat::A2R10G10B10:
    case PixelFormat::A2R10G10B10Srgb:
    case PixelFormat::A2R10G10B10Bt2020Pq:
        return true;
    default:
        return false;
    }
}

constexpr u32 PixelFormatBpp(PixelFormat pixel_format) {
    switch (pixel_format) {
    case PixelFormat::A16R16G16B16Float:
        return 8;
    default:
        return 4;
    }
}

VideoOutDriver::VideoOutDriver(u32 width, u32 height) {
    main_port.resolution.full_width = width;
    main_port.resolution.full_height = height;
    main_port.resolution.pane_width = width;
    main_port.resolution.pane_height = height;
    present_thread = std::jthread([&](std::stop_token token) { PresentThread(token); });
}

VideoOutDriver::~VideoOutDriver() = default;

int VideoOutDriver::Open(const ServiceThreadParams* params) {
    if (main_port.is_open) {
        return ORBIS_VIDEO_OUT_ERROR_RESOURCE_BUSY;
    }
    main_port.is_open = true;
    liverpool->SetVoPort(&main_port);
    return 1;
}

void VideoOutDriver::Close(s32 handle) {
    std::scoped_lock lock{mutex};

    // Mark as closed
    main_port.is_open = false;
    main_port.flip_rate = 0;
    main_port.prev_index = -1;

    // Clear port information
    std::memset(main_port.buffer_labels.data(), 0, sizeof(main_port.buffer_labels));
    std::memset(main_port.groups.data(), 0, sizeof(main_port.groups));
    std::memset(&main_port.vblank_status, 0, sizeof(main_port.vblank_status));
    main_port.flip_status = FlipStatus{};

    // Re-initialize buffers
    std::memset(main_port.buffer_slots.data(), 0, sizeof(main_port.buffer_slots));
    for (auto& buffer : main_port.buffer_slots) {
        buffer.group_index = -1;
    }

    // Clear events
    for (auto event : main_port.flip_events) {
        auto equeue = Kernel::GetEqueue(event);
        if (equeue != nullptr) {
            equeue->RemoveEvent(static_cast<u64>(OrbisVideoOutInternalEventId::Flip),
                                Kernel::OrbisKernelEvent::Filter::VideoOut);
        }
    }
    main_port.flip_events.clear();
    for (auto event : main_port.vblank_events) {
        auto equeue = Kernel::GetEqueue(event);
        if (equeue != nullptr) {
            equeue->RemoveEvent(static_cast<u64>(OrbisVideoOutInternalEventId::Vblank),
                                Kernel::OrbisKernelEvent::Filter::VideoOut);
        }
    }
    main_port.vblank_events.clear();
}

VideoOutPort* VideoOutDriver::GetPort(int handle) {
    if (handle != 1) [[unlikely]] {
        return nullptr;
    }
    return &main_port;
}

int VideoOutDriver::RegisterBuffers(VideoOutPort* port, s32 startIndex, void* const* addresses,
                                    s32 bufferNum, const BufferAttribute* attribute) {
    const s32 group_index = port->FindFreeGroup();
    if (group_index >= MaxDisplayBufferGroups) {
        return ORBIS_VIDEO_OUT_ERROR_NO_EMPTY_SLOT;
    }

    if (startIndex + bufferNum > MaxDisplayBuffers || startIndex > MaxDisplayBuffers ||
        bufferNum > MaxDisplayBuffers) {
        LOG_ERROR(Lib_VideoOut,
                  "Attempted to register too many buffers startIndex = {}, bufferNum = {}",
                  startIndex, bufferNum);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_VALUE;
    }

    const s32 end_index = startIndex + bufferNum;
    if (bufferNum > 0 &&
        std::any_of(port->buffer_slots.begin() + startIndex, port->buffer_slots.begin() + end_index,
                    [](auto& buffer) { return buffer.group_index != -1; })) {
        return ORBIS_VIDEO_OUT_ERROR_SLOT_OCCUPIED;
    }

    if (attribute->reserved0 != 0 || attribute->reserved1 != 0) {
        LOG_ERROR(Lib_VideoOut, "Invalid reserved members");
        return ORBIS_VIDEO_OUT_ERROR_INVALID_VALUE;
    }
    if (attribute->aspect_ratio != 0) {
        LOG_ERROR(Lib_VideoOut, "Invalid aspect ratio = {}", attribute->aspect_ratio);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_ASPECT_RATIO;
    }
    if (attribute->width > attribute->pitch_in_pixel) {
        LOG_ERROR(Lib_VideoOut, "Buffer width {} is larger than pitch {}", attribute->width,
                  attribute->pitch_in_pixel);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_PITCH;
    }
    if (attribute->tiling_mode < TilingMode::Tile || attribute->tiling_mode > TilingMode::Linear) {
        LOG_ERROR(Lib_VideoOut, "Invalid tilingMode = {}",
                  static_cast<u32>(attribute->tiling_mode));
        return ORBIS_VIDEO_OUT_ERROR_INVALID_TILING_MODE;
    }

    LOG_INFO(Lib_VideoOut,
             "startIndex = {}, bufferNum = {}, pixelFormat = {}, aspectRatio = {}, "
             "tilingMode = {}, width = {}, height = {}, pitchInPixel = {}, option = {:#x}",
             startIndex, bufferNum, GetPixelFormatString(attribute->pixel_format),
             attribute->aspect_ratio, static_cast<u32>(attribute->tiling_mode), attribute->width,
             attribute->height, attribute->pitch_in_pixel, attribute->option);

    auto& group = port->groups[group_index];
    std::memcpy(&group.attrib, attribute, sizeof(BufferAttribute));
    group.is_occupied = true;

    for (u32 i = 0; i < bufferNum; i++) {
        const uintptr_t address = reinterpret_cast<uintptr_t>(addresses[i]);
        port->buffer_slots[startIndex + i] = VideoOutBuffer{
            .group_index = group_index,
            .address_left = address,
            .address_right = 0,
        };

        // Reset flip label also when registering buffer
        port->buffer_labels[startIndex + i] = 0;
        port->SignalVoLabel();

        presenter->RegisterVideoOutSurface(group, address);
        LOG_INFO(Lib_VideoOut, "buffers[{}] = {:#x}", i + startIndex, address);
    }

    return group_index;
}

int VideoOutDriver::UnregisterBuffers(VideoOutPort* port, s32 attributeIndex) {
    if (attributeIndex >= MaxDisplayBufferGroups || !port->groups[attributeIndex].is_occupied) {
        LOG_ERROR(Lib_VideoOut, "Invalid attribute index {}", attributeIndex);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_VALUE;
    }

    auto& group = port->groups[attributeIndex];
    group.is_occupied = false;

    for (auto& buffer : port->buffer_slots) {
        if (buffer.group_index != attributeIndex) {
            continue;
        }
        buffer.group_index = -1;
    }

    return ORBIS_OK;
}

int VideoOutDriver::ChangeBufferAttribute(VideoOutPort* port, s32 attributeIndex,
                                          const BufferAttribute* attribute) {
    if (attributeIndex >= MaxDisplayBufferGroups || !port->groups[attributeIndex].is_occupied) {
        LOG_ERROR(Lib_VideoOut, "Invalid attribute index {}", attributeIndex);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_VALUE;
    }

    if (attribute->reserved0 != 0 || attribute->reserved1 != 0) {
        LOG_ERROR(Lib_VideoOut, "Invalid reserved members");
        return ORBIS_VIDEO_OUT_ERROR_INVALID_VALUE;
    }
    if (attribute->aspect_ratio != 0) {
        LOG_ERROR(Lib_VideoOut, "Invalid aspect ratio = {}", attribute->aspect_ratio);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_ASPECT_RATIO;
    }
    if (attribute->width > attribute->pitch_in_pixel) {
        LOG_ERROR(Lib_VideoOut, "Buffer width {} is larger than pitch {}", attribute->width,
                  attribute->pitch_in_pixel);
        return ORBIS_VIDEO_OUT_ERROR_INVALID_PITCH;
    }
    if (attribute->tiling_mode < TilingMode::Tile || attribute->tiling_mode > TilingMode::Linear) {
        LOG_ERROR(Lib_VideoOut, "Invalid tilingMode = {}",
                  static_cast<u32>(attribute->tiling_mode));
        return ORBIS_VIDEO_OUT_ERROR_INVALID_TILING_MODE;
    }

    LOG_INFO(Lib_VideoOut,
             "attributeIndex = {}, pixelFormat = {}, aspectRatio = {}, "
             "tilingMode = {}, width = {}, height = {}, pitchInPixel = {}, option = {:#x}",
             attributeIndex, GetPixelFormatString(attribute->pixel_format), attribute->aspect_ratio,
             static_cast<u32>(attribute->tiling_mode), attribute->width, attribute->height,
             attribute->pitch_in_pixel, attribute->option);

    std::unique_lock lock{port->port_mutex};
    std::memcpy(&port->groups[attributeIndex].attrib, attribute, sizeof(BufferAttribute));
    return 0;
}

void VideoOutDriver::Flip(const Request& req) {
    // Update HDR status before presenting.
    presenter->SetHDR(req.port->is_hdr);

    // Present the frame.
    presenter->Present(req.frame);

    // sotc: SOTC_FPS=1 prints the flip rate every five seconds; SOTC_PRESENT_STATS=1 and
    // SOTC_THREAD_STATS=1 add the present thread's and the rendering threads' numbers and work on
    // their own too.
    static const bool fps_log = std::getenv("SOTC_FPS") != nullptr ||
                                std::getenv("SOTC_PRESENT_STATS") != nullptr ||
                                SotcStats::ThreadStatsEnabled();
    if (fps_log) {
        static auto window_start = std::chrono::steady_clock::now();
        static const auto run_start = window_start;
        static u32 flips = 0;
        ++flips;
        const auto now = std::chrono::steady_clock::now();
        if (now - window_start >= std::chrono::seconds(5)) {
            const double seconds = std::chrono::duration<double>(now - window_start).count();
            const uint64_t readbacks = SotcStats::readbacks.exchange(0, std::memory_order_relaxed);
            const uint64_t readback_ns =
                SotcStats::readback_ns.exchange(0, std::memory_order_relaxed);
            const uint64_t copies = SotcStats::readback_copies.exchange(0);
            const uint64_t copies_changed = SotcStats::readback_copies_changed.exchange(0);
            const uint64_t bytes = SotcStats::readback_bytes.exchange(0);
            const uint64_t bytes_changed = SotcStats::readback_bytes_changed.exchange(0);
            std::fprintf(stderr,
                         "SOTC FPS %.1f at %.0fs; %.0f cmds/frame; readbacks %.0f/s (ahead %llu, finish %llu, heap %llu, early %llu), %.0f ms/s; "
                         "changed copies %llu/%llu, bytes %llu/%llu\n",
                         flips / seconds, std::chrono::duration<double>(now - run_start).count(),
                         flips ? double(SotcStats::commands.exchange(0)) / flips : 0.0,
                         readbacks / seconds,
                         static_cast<unsigned long long>(SotcStats::readback_ahead.exchange(0)),
                         static_cast<unsigned long long>(SotcStats::readback_finish.exchange(0)),
                         static_cast<unsigned long long>(SotcStats::heap_guard_builds.exchange(0)),
                         static_cast<unsigned long long>(SotcStats::early_flushes.exchange(0)),
                         readback_ns / 1e6 / seconds,
                         static_cast<unsigned long long>(copies_changed),
                         static_cast<unsigned long long>(copies),
                         static_cast<unsigned long long>(bytes_changed),
                         static_cast<unsigned long long>(bytes));
            static const bool cmd_stats = std::getenv("SOTC_CMD_STATS") != nullptr;
            if (cmd_stats && flips) {
                double k[6];
                for (int i = 0; i < 6; ++i) {
                    k[i] = double(SotcStats::cmd_kinds[i].exchange(0)) / flips;
                }
                std::fprintf(stderr,
                             "SOTC cmds/frame: draw %.0f, indirect %.0f gpu-args + %.0f cpu-args "
                             "(%.0f empty), dispatch %.0f, dispatch-indirect %.0f\n",
                             k[0], k[1], k[2], k[3], k[4], k[5]);
                std::fprintf(stderr,
                             "SOTC faults/s: guest writes %.0f, walker loads %.0f, CP reads %.0f\n",
                             SotcStats::fault_writes.exchange(0) / seconds,
                             SotcStats::fault_walker.exchange(0) / seconds,
                             SotcStats::fault_gpu_reads.exchange(0) / seconds);
                std::fprintf(stderr,
                             "SOTC per frame: uploads %.0f (%.0f KB), render pass breaks %.0f\n",
                             double(SotcStats::uploads.exchange(0)) / flips,
                             double(SotcStats::upload_bytes.exchange(0)) / 1024.0 / flips,
                             double(SotcStats::render_breaks.exchange(0)) / flips);
            }
            static const bool draw_stats_log = SotcStats::DrawStatsEnabled();
            if (draw_stats_log) {
                const double samples = double(SotcStats::draw_stat_cmds.exchange(0));
                const auto per_draw_us = [samples](std::atomic<uint64_t>& ns) {
                    return samples > 0 ? double(ns.exchange(0)) / samples / 1000.0 : 0.0;
                };
                std::fprintf(stderr,
                             "SOTC draw us (%.0f sampled): key %.2f (stages %.2f [getprogram "
                             "%.2f of which flatbuf %.2f], lookup %.2f), prepare %.2f, bind %.2f "
                             "(buf %.2f, tex %.2f), state %.2f, emit %.2f\n",
                             samples, per_draw_us(SotcStats::draw_key_ns),
                             per_draw_us(SotcStats::draw_stages_ns),
                             per_draw_us(SotcStats::draw_getprogram_ns),
                             per_draw_us(SotcStats::draw_flatbuf_ns),
                             per_draw_us(SotcStats::draw_lookup_ns),
                             per_draw_us(SotcStats::draw_prepare_ns),
                             per_draw_us(SotcStats::draw_bind_ns),
                             per_draw_us(SotcStats::draw_bufbind_ns),
                             per_draw_us(SotcStats::draw_texbind_ns),
                             per_draw_us(SotcStats::draw_state_ns),
                             per_draw_us(SotcStats::draw_emit_ns));
                const double key_total = double(SotcStats::draw_key_total.exchange(0));
                const double key_repeats = double(SotcStats::draw_key_repeats.exchange(0));
                std::fprintf(stderr, "SOTC draw key repeats: %.1f%% of %.0f draws\n",
                             key_total > 0 ? 100.0 * key_repeats / key_total : 0.0, key_total);
            }
            static const bool present_stats_log = std::getenv("SOTC_PRESENT_STATS") != nullptr;
            if (present_stats_log) {
                const auto cpu_now = PresentThreadCpuNs();
                std::fprintf(
                    stderr,
                    "SOTC presenter: %.0f vblanks/s, %.1f frames/s, present %.1f avg / %.1f max "
                    "ms (%llu), catchup %llu, min wait %.2f ms, cpu %.0f ms/s, %s\n",
                    static_cast<double>(present_stats.iterations) / seconds,
                    static_cast<double>(present_stats.presents) / seconds,
                    present_stats.presents ? static_cast<double>(present_stats.present_ns) / 1e6 /
                                                 present_stats.presents
                                           : 0.0,
                    static_cast<double>(present_stats.max_present_ns) / 1e6,
                    static_cast<unsigned long long>(present_stats.presents),
                    static_cast<unsigned long long>(present_stats.catchup),
                    static_cast<double>(present_stats.min_wait_ns) / 1e6,
                    static_cast<double>(cpu_now - present_stats.cpu_ns) / 1e6 / seconds,
                    present_realtime ? "realtime" : "normal");
                present_stats.cpu_ns = cpu_now;
            }
            if (SotcStats::ThreadStatsEnabled()) {
                const double every = static_cast<double>(SotcStats::BusySampleEvery());
                std::fprintf(
                    stderr,
                    "SOTC threads: cp %.0f ms/s busy (%.0f cmds/s, sampled 1/%llu), replay %.0f "
                    "busy + %.0f spin + %.0f sleep ms/s (%.0f cmds/s), flushes %.0f/s (%.0f ms/s), "
                    "backpressure %.1f/s\n",
                    double(SotcStats::cp_busy_ns.exchange(0)) * every / 1e6 / seconds,
                    double(SotcStats::cp_cmds.exchange(0)) * every / seconds,
                    static_cast<unsigned long long>(SotcStats::BusySampleEvery()),
                    double(SotcStats::replay_busy_ns.exchange(0)) / 1e6 / seconds,
                    double(SotcStats::replay_spin_ns.exchange(0)) / 1e6 / seconds,
                    double(SotcStats::replay_sleep_ns.exchange(0)) / 1e6 / seconds,
                    double(SotcStats::replay_cmds.exchange(0)) / seconds,
                    double(SotcStats::flushes.exchange(0)) / seconds,
                    double(SotcStats::flush_ns.exchange(0)) / 1e6 / seconds,
                    double(SotcStats::recorder_backpressure.exchange(0)) / seconds);
            }
            present_stats.iterations = 0;
            present_stats.presents = 0;
            present_stats.present_ns = 0;
            present_stats.max_present_ns = 0;
            present_stats.catchup = 0;
            present_stats.min_wait_ns = 0;
            flips = 0;
            window_start = now;
        }
    }

    // Update flip status.
    auto* port = req.port;
    {
        std::unique_lock lock{port->port_mutex};
        auto& flip_status = port->flip_status;
        flip_status.count++;
        flip_status.process_time = Libraries::Kernel::sceKernelGetProcessTime();
        flip_status.tsc = Libraries::Kernel::sceKernelReadTsc();
        flip_status.flip_arg = req.flip_arg;
        flip_status.current_buffer = req.index;
        if (req.eop) {
            --flip_status.gc_queue_num;
        }
        --flip_status.flip_pending_num;
    }

    // Trigger flip events for the port.
    for (auto event : port->flip_events) {
        auto equeue = Kernel::GetEqueue(event);
        if (equeue != nullptr) {
            equeue->TriggerEvent(
                static_cast<u64>(OrbisVideoOutInternalEventId::Flip),
                Kernel::OrbisKernelEvent::Filter::VideoOut,
                reinterpret_cast<void*>(static_cast<u64>(OrbisVideoOutInternalEventId::Flip) |
                                        (req.flip_arg << 16)));
        }
    }

    // Reset prev flip label
    if (port->prev_index != -1) {
        port->buffer_labels[port->prev_index] = 0;
        port->SignalVoLabel();
    }
    // save to prev buf index
    port->prev_index = req.index;
}

void VideoOutDriver::DrawBlankFrame() {
    const auto empty_frame = presenter->PrepareBlankFrame(true);
    presenter->Present(empty_frame, false, false);
}

void VideoOutDriver::DrawLastFrame() {
    const auto frame = presenter->PrepareLastFrame();
    if (frame != nullptr) {
        presenter->Present(frame, true);
    }
}

bool VideoOutDriver::SubmitFlip(VideoOutPort* port, s32 index, s64 flip_arg,
                                bool is_eop /*= false*/) {
    {
        std::unique_lock lock{port->port_mutex};
        if (index != -1 && port->flip_status.flip_pending_num > 16) {
            LOG_ERROR(Lib_VideoOut, "Flip queue is full");
            return false;
        }

        if (is_eop) {
            ++port->flip_status.gc_queue_num;
        }
        ++port->flip_status.flip_pending_num; // integral GPU and CPU pending flips counter
        port->flip_status.submit_tsc = Libraries::Kernel::sceKernelReadTsc();
    }

    if (!is_eop) {
        // Non EOP flips can arrive from any thread so ask GPU thread to perform them
        liverpool->SendCommand([=, this]() { SubmitFlipInternal(port, index, flip_arg, is_eop); });
    } else {
        SubmitFlipInternal(port, index, flip_arg, is_eop);
    }

    return true;
}

void VideoOutDriver::SubmitFlipInternal(VideoOutPort* port, s32 index, s64 flip_arg, bool is_eop) {
    Vulkan::Frame* frame;
    if (index == -1) {
        frame = presenter->PrepareBlankFrame(false);
    } else {
        const auto& buffer = port->buffer_slots[index];
        ASSERT_MSG(buffer.group_index >= 0, "Trying to flip an unregistered buffer!");
        const auto& group = port->groups[buffer.group_index];
        frame = presenter->PrepareFrame(group, buffer.address_left);
    }

    std::scoped_lock lock{mutex};
    requests.push({
        .frame = frame,
        .port = port,
        .flip_arg = flip_arg,
        .index = index,
        .eop = is_eop,
    });
}

void VideoOutDriver::PresentThread(std::stop_token token) {
    const std::chrono::nanoseconds vblank_period(1000000000 /
                                                 EmulatorSettings.GetVblankFrequency());

    Common::SetCurrentThreadName("shadPS4:PresentThread");
    // sotc: SOTC_PRESENT_RT=0 leaves the presenter on the normal scheduler instead of macOS's
    // real-time time-constraint policy (half a vblank period of CPU). It is the A/B switch of
    // the high-vblank slow-mode study; SOTC_PRESENT_STATS=1 prints the numbers it measures.
    present_realtime = [] {
        const char* v = std::getenv("SOTC_PRESENT_RT");
        return v == nullptr || v[0] != '0';
    }();
    if (present_realtime) {
        Common::SetCurrentThreadRealtime(vblank_period);
    }
    present_stats.cpu_ns = PresentThreadCpuNs();

    Common::AccurateTimer timer{vblank_period};

    const auto receive_request = [this] -> Request {
        std::scoped_lock lk{mutex};
        if (!requests.empty()) {
            const auto request = requests.front();
            requests.pop();
            return request;
        }
        return {};
    };

    while (!token.stop_requested()) {
        timer.Start();
        ++present_stats.iterations;

        if (DebugState.IsGuestThreadsPaused()) {
            DrawLastFrame();
            timer.End();
            continue;
        }

        // Check if it's time to take a request.
        auto& vblank_status = main_port.vblank_status;
        if (vblank_status.count % (main_port.flip_rate + 1) == 0) {
            const auto request = receive_request();
            if (!request) {
                if (timer.GetTotalWait().count() < 0) { // Dont draw too fast
                    if (!main_port.is_open) {
                        DrawBlankFrame();
                    } else if (ImGui::Core::MustKeepDrawing()) {
                        DrawLastFrame();
                    }
                }
            } else {
                const auto present_begin = std::chrono::steady_clock::now();
                Flip(request);
                const auto present_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            std::chrono::steady_clock::now() - present_begin)
                                            .count();
                ++present_stats.presents;
                present_stats.present_ns += static_cast<u64>(present_ns);
                if (static_cast<u64>(present_ns) > present_stats.max_present_ns) {
                    present_stats.max_present_ns = static_cast<u64>(present_ns);
                }
                FRAME_END;
            }
        }

        {
            // Needs lock here as can be concurrently read by `sceVideoOutGetVblankStatus`
            std::scoped_lock lock{main_port.vo_mutex};

            // Trigger flip events for the port
            for (auto event : main_port.vblank_events) {
                auto equeue = Kernel::GetEqueue(event);
                if (equeue != nullptr) {
                    equeue->TriggerEvent(
                        static_cast<u64>(OrbisVideoOutInternalEventId::Vblank),
                        Kernel::OrbisKernelEvent::Filter::VideoOut,
                        reinterpret_cast<void*>(
                            static_cast<u64>(OrbisVideoOutInternalEventId::Vblank) |
                            (vblank_status.count << 16)));
                }
            }

            // Update vblank status
            vblank_status.count++;
            vblank_status.process_time = Libraries::Kernel::sceKernelGetProcessTime();
            vblank_status.tsc = Libraries::Kernel::sceKernelReadTsc();
            main_port.vblank_cv.notify_all();
        }

        timer.End();
        const auto wait = timer.GetTotalWait().count();
        if (wait < 0) {
            ++present_stats.catchup;
            if (wait < present_stats.min_wait_ns) {
                present_stats.min_wait_ns = wait;
            }
        }
    }
}

} // namespace Libraries::VideoOut
