# Shadow of the Colossus on Apple Silicon (macOS) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and run the Shadow of the Colossus native port (`ShadowOfTheColossus-PC`) on Apple Silicon macOS (under Rosetta 2) using SDL3 in windowed mode without locking the mouse cursor to the application.

**Architecture:** macOS is added alongside Linux and Windows using the static-library linkage model (`bbgpu` linked into `bb-probe` by CMake with `-DCMAKE_OSX_ARCHITECTURES=x86_64` and `-Wl,-pagezero_size,0x400000`). The process main thread serves SDL3's Cocoa/Metal window (`SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN`, `SDL_Metal_CreateView`, `VK_EXT_metal_surface`) in windowed mode with relative mouse mode and mouse grab disabled (`SDL_SetWindowRelativeMouseMode(window, false)`, `SDL_SetWindowMouseGrab(window, false)`), while a 32 MiB pthread runs the loader, SSE4a CPU patches, and guest code over a sub-2 GiB eboot mapping and `shm_open`-backed guest memory.

**Tech Stack:** Xcode Clang (`-arch x86_64`), CMake >= 3.24 + Ninja, vcpkg (`x64-osx` static triplet), SDL3 (`<SDL3/SDL.h>`, `<SDL3/SDL_metal.h>`), Vulkan 1.3 (vcpkg loader + Mesa KosmicKrisp x86-64 / MoltenVK fallback), Python 3 with Tkinter.

**Spec:** `docs/superpowers/specs/2026-10-14-macos-apple-silicon-design.md`

## Global Constraints

- Target platform: Apple Silicon macOS 15.4+ for Rosetta 2 AVX2/BMI1/FMA/MOVBE translation; macOS 26+ for Mesa KosmicKrisp Vulkan 1.3 driver.
- Game process architecture: `x86_64` (`-DCMAKE_OSX_ARCHITECTURES=x86_64`) linked with `-Wl,-pagezero_size,0x400000` so the eboot and its 16 MiB trampoline region map in `[0x400000, 0x80000000)` (below 2 GiB for the game's 31-bit static pointers).
- Guest memory reservations on Apple (`PROT_NONE`, `MAP_FIXED | MAP_NORESERVE` first thing in `main`):
  - System-managed low window: `SYSTEM_LOW_MIN = 0x0700000000`, `SYSTEM_LOW_MAX = 0x0800000000`.
  - Host low map & arena: `LOW_MIN = 0x0800000000`, `LOW_MAX = 0x0fc0000000`.
  - User range: `USER_MIN = 0x7000000000`, `USER_MAX = 0xfc00000000`.
- Windowed mode is mandatory by default: `BB_FULLSCREEN` defaults to `'0'`, `SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN` is `false` unless `BB_FULLSCREEN=1` is explicitly set, and windowed dimensions exceeding 90% of `SDL_GetDisplayUsableBounds` are scaled down to fit within 90% of usable screen bounds.
- Mouse cursor must never be locked to the application: `SDL_SetWindowRelativeMouseMode(window, false)`, `SDL_SetWindowMouseGrab(window, false)`, and `SDL_ShowCursor()` are enforced on window creation; `WindowSDL::PollEvents()` never enables relative mouse mode or mouse grab on focus gain or mouse clicks; `SOTC_MOUSE` defaults to `'0'` in `window.cpp`, `sotc.py`, and `launcher/sotc_launcher_win.py` (`APP_DEFAULTS['mouse'] = False`).
- Protection faults on macOS arrive as `SIGBUS` (as well as `SIGSEGV`) and illegal instructions (unpatched 4-byte SSE4a) as `SIGILL`; all signal handlers run with `SA_SIGINFO | SA_ONSTACK` on a per-thread 512 KiB `sigaltstack`.

## Review Focus

1. **Mouse click or focus gain inside the SDL window must not lock or hide the cursor:** Clicking the window (`SDL_EVENT_MOUSE_BUTTON_DOWN`) or focusing it (`SDL_EVENT_WINDOW_FOCUS_GAINED`) must leave `SDL_GetWindowRelativeMouseMode(window) == false`, `SDL_GetWindowMouseGrab(window) == false`, and `bbgpu_mouse_take(...) == 0`. Pinned by `test_window_unlocked_mouse` in Task 5.
2. **Default window creation must be windowed (not fullscreen) and fit the screen:** With `BB_FULLSCREEN` unset or `"0"`, `(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0`. Pinned by `test_window_unlocked_mouse` in Task 5.
3. **Eboot reservation below 2 GiB on macOS:** Without `-Wl,-pagezero_size,0x400000`, `__PAGEZERO` occupies `0..4 GiB` and `reserve_eboot_range()` fails to find 256 MiB below `0x80000000`. Pinned by `test_eboot_reservation_below_2gb` in Task 4.
4. **Stale POSIX shared-memory object from a crashed run with the same PID:** `shm_open` with `O_CREAT | O_EXCL` fails if `/bb-guest-<pid>` was left behind. Pinned by `pool()` calling `shm_unlink` before `shm_open`, exercised by `runtime-test` in Task 3.
5. **Stopping the game from the launcher on macOS when child processes are running or ignoring `SIGTERM`:** `GameJob.terminate()` must kill the whole process group started with `start_new_session=True` and escalate to `SIGKILL`. Pinned by `tests/test_launcher_platform.py` in Task 6.

---

## File Structure

| File | Responsibility |
|---|---|
| `packaging/macos/vcpkg.json` | **Create.** vcpkg manifest (`x64-osx` static triplet) for SDL3, Vulkan, FFmpeg, Boost, fmt, Zydis, VMA, xxhash, robin-map. |
| `gpu/shim/macos/bb_mac_compat.h` | **Create.** Force-included Apple header providing `gettid()` via `pthread_threadid_np` for the GPU library. |
| `src/compat_mac.h`, `src/compat_mac.c` | **Create.** macOS POSIX polyfills (`clock_nanosleep`, timed mutex/rwlock locks, `getrandom`, `getrusage(RUSAGE_THREAD)`, `pthread_barrier_*`, `pthread_setname_np`, `st_*tim`). |
| `tests/test_compat_mac.c` | **Create.** Unit tests for `compat_mac`, GS pthread-key TLS slot read, and macOS Rosetta version check. |
| `gpu/CMakeLists.txt` | **Modify.** `WIN32 OR APPLE` static `bbgpu`, `bb_join_frameworks`, `-framework QuartzCore -framework Metal`, `-Wl,-pagezero_size,0x400000` on `bb-probe`, and macOS C test targets. |
| `build.sh` | **Modify.** `Darwin*` build path using vcpkg (`x64-osx`), `MoltenVK` (`x86_64` slice check), and optional `--test` execution. |
| `src/runtime.h` | **Modify.** Include `compat_mac.h` on POSIX, declare `runtime_guest_alloc`/`runtime_guest_free` and `runtime_sotc_heap`/`runtime_sotc_set_heap`. |
| `src/runtime_sema.c` | **Modify.** Use `CLOCK_REALTIME` for `WAIT_CLOCK` on `__APPLE__`. |
| `src/runtime_thread.c` | **Modify.** Apple TLS key (`pthread_once` + `8 * tls_key` GS offset) and 512 KiB `sigaltstack` installation per attached thread. |
| `src/runtime_sotc.c` | **Modify.** Apple `get_thread_id` (`pthread_threadid_np`), `runtime_guest_alloc` for guest-visible handles, and `runtime_sotc_heap`/`runtime_sotc_set_heap`. |
| `src/runtime_memory.c` | **Modify.** Apple `USER_MIN`/`LOW_MAX`, `shm_open` pool, `runtime_memory_reserve()` for `SYSTEM_LOW`, `LOW`, and `USER` ranges, `PROT_NONE` re-reservation on unmap, and `runtime_guest_alloc` low arena. |
| `src/runtime_mutex.c`, `src/runtime_rwlock.c`, `src/runtime.c` | **Modify.** Allocate guest-visible mutex, cond, rwlock, and dynamic TLS blocks from `runtime_guest_alloc`. |
| `src/probe.c` | **Modify.** Apple sub-2 GiB `reserve_eboot_range()` (`mach_vm_region`), `rosetta_ok()` CPU check, Darwin `ucontext_t` (`SIGBUS`/`SIGSEGV` -> `bbgpu_handle_fault`, `SIGILL` -> `sotc_handle_illegal`), `SA_ONSTACK`, `patch_tcb_loads`, and main-thread window loop / `loader_thread` split. |
| `gpu/sotc/cpu_patches.cpp` | **Modify.** Enable `NeedsSse4aEmulation()`, `PatchShortSse4aInstructions()`, `PatchRedZoneMemoryInstructions()`, and `PrePatchInstructions()` on `__APPLE__`. |
| `gpu/bbgpu.h`, `gpu/shim/bbgpu.cpp` | **Modify.** Declare and implement `bbgpu_run_window_loop(void)` so the macOS main thread creates and pumps `WindowSDL`. |
| `gpu/shim/sdl_window.h`, `gpu/shim/window.cpp` | **Modify.** Create `SDL_MetalView` + `CAMetalLayer` on `cocoa`, enforce windowed mode by default (with 90% usable desktop size clamping), and disable all mouse cursor locking/grabbing (`SDL_SetWindowRelativeMouseMode(window, false)`, `SDL_SetWindowMouseGrab(window, false)`). |
| `gpu/shadps4/common/path_util.cpp`, `gpu/shadps4/video_core/amdgpu/liverpool.cpp`, `gpu/shadps4/video_core/amdgpu/liverpool.h`, `gpu/shadps4/video_core/amdgpu/pm4_cmds.h`, `gpu/shadps4/video_core/renderer_vulkan/vk_instance.cpp`, `gpu/shadps4/video_core/renderer_vulkan/vk_platform.cpp`, `gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp`, `gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.h` | **Modify.** Apple portability enumeration, non-overwriting `VK_DRIVER_FILES`, optional `robustness2` features on MoltenVK, `VAddr` cast in `pm4_cmds.h`, `gettid()` and Linux-only per-thread rusage guards, and `BB_GPU_USER_DIR` creation. |
| `gpu/shim/bbport_overlay.cpp`, `gpu/shim/bbport_process_memory.cpp`, `gpu/shim/bbport_threads.h`, `gpu/shim/bbport_write_log.cpp`, `gpu/shim/core/libraries/kernel/threads.h` | **Modify.** Mach-O `.incbin` section syntax, `task_info` memory stats, Darwin background QoS, Darwin `ucontext_t` registers, and self-join guard in `Libraries::Kernel::Thread::Join`. |
| `src/vulkan_smoke.c`, `tools/gpu_capabilities.c` | **Modify.** Enable `VK_KHR_portability_enumeration` on `__APPLE__`. |
| `tests/test_gpu_runtime_stubs.cpp`, `tests/test_pad.c`, `tests/test_runtime.c` | **Modify.** Add missing runtime stubs for static `bbgpu`, add low-arena / TLS / eboot reservation checks, and test SDL windowed mode + unlocked mouse cursor. |
| `tools/build_kosmickrisp.sh` | **Create.** Build (or copy from local cache) Mesa's x86-64 `libvulkan_kosmickrisp.dylib`, `libz.1.dylib`, and `kosmickrisp_mesa_icd.json` into `out/`. |
| `sotc.py`, `run.sh` | **Modify.** macOS `bb-probe` lookup, `DYLD_LIBRARY_PATH` and `VK_DRIVER_FILES` (preferring KosmicKrisp), default `BB_FULLSCREEN=0` and `SOTC_MOUSE=0`. |
| `launcher/sotc_launcher_win.py`, `launcher/sotc_discovery.py`, `launcher/sotc-launcher.sh`, `tests/test_launcher_platform.py` | **Modify / Create.** Cross-platform Tkinter launcher for macOS (`sotc.py` runner, `GameJob` process-group termination, `APP_DEFAULTS['fullscreen'] = False`, `APP_DEFAULTS['mouse'] = False`, macOS drive/folder discovery) and headless platform unit tests. |

---

### Task 1: Build Scaffolding (`packaging/macos/vcpkg.json`, `gpu/CMakeLists.txt`, `build.sh`)

**Files:**
- Create: `packaging/macos/vcpkg.json`
- Create: `gpu/shim/macos/bb_mac_compat.h`
- Create: `src/compat_mac.h`, `src/compat_mac.c`
- Modify: `gpu/CMakeLists.txt:63-70`, `:134-141`, `:161-202`
- Modify: `build.sh:7-37`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `packaging/macos/vcpkg.json` (manifest for `x64-osx`).
  - `gpu/shim/macos/bb_mac_compat.h`: `static inline int gettid(void)`.
  - CMake targets on `APPLE`: `bb-probe` (linked with `-Wl,-pagezero_size,0x400000`), `bb-gpu-capabilities`, `compat-mac-test`, `pad-test`, `file-mods-test`, `content-test`, `runtime-test`, `sema-test`.
  - `build.sh` `Darwin*` branch producing `out/bb-probe`, `out/bb-gpu-capabilities`, `out/libvulkan*.dylib`, `out/libMoltenVK.dylib`, `out/MoltenVK_icd.json`.

- [ ] **Step 1: Verify `build.sh` currently fails on Darwin**

Run: `bash build.sh`
Expected: FAIL with `Need pkg-config with vulkan and sdl3, cmake and ninja (see shell.nix).` or Linux-specific linker flags (`-Wl,--allow-shlib-undefined`).

- [ ] **Step 2: Create `packaging/macos/vcpkg.json` and `gpu/shim/macos/bb_mac_compat.h`**

Create `packaging/macos/vcpkg.json`:
```json
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
  "name": "sotc-macos",
  "version-string": "0.57",
  "description": "x86-64 dependencies of the Shadow of the Colossus port on macOS (built under Rosetta 2)",
  "dependencies": [
    "sdl3",
    "vulkan-headers",
    "vulkan-loader",
    "vulkan-memory-allocator",
    "fmt",
    "boost-asio",
    "boost-container",
    "boost-container-hash",
    "boost-icl",
    "boost-intrusive",
    "boost-pool",
    "boost-preprocessor",
    "robin-map",
    "xxhash",
    { "name": "ffmpeg", "default-features": false, "features": ["avcodec", "avformat", "swresample", "swscale"] },
    "zydis"
  ]
}
```

Create `gpu/shim/macos/bb_mac_compat.h` defining `static inline int gettid(void)` via `pthread_threadid_np(NULL, &id)`.

- [ ] **Step 3: Update `gpu/CMakeLists.txt` and `build.sh` for `APPLE` / `Darwin*`**

In `gpu/CMakeLists.txt`:
- Add `bb_join_frameworks(target)` and call it on `PkgConfig::XXHASH`, `PkgConfig::SDL3`, and `PkgConfig::FFMPEG` when `APPLE` is true.
- Guard `pkg_check_modules(X11 ...)` and `-Wl,--allow-shlib-undefined` with `if (NOT WIN32 AND NOT APPLE)`.
- Use `set(BBGPU_KIND STATIC)` for `if (WIN32 OR APPLE)`.
- Add `elseif (APPLE)` branch to `bbgpu`: include `third_party/xbyak` and `shim/macos`, force-include `${CMAKE_CURRENT_SOURCE_DIR}/shim/macos/bb_mac_compat.h`, and link `Vulkan::Vulkan "-framework QuartzCore" "-framework Metal"`.
- Widen the `if (WIN32)` executable block to `if (WIN32 OR APPLE)`:
  - On `APPLE`, append `../src/compat_mac.c` to `bb-probe`, link `bbgpu atrac9 Vulkan::Vulkan -pthread`, pass `target_link_options(bb-probe PRIVATE "-Wl,-pagezero_size,0x400000")`, and set `BUILD_RPATH "@executable_path"` on `bb-probe` and `bb-gpu-capabilities`.
  - Add `EXCLUDE_FROM_ALL` test targets on `APPLE`: `compat-mac-test`, `pad-test`, `file-mods-test`, `content-test`, `runtime-test`, `sema-test` compiled with `-UNDEBUG -Wall -Wextra -Werror` (and `-Wno-error=unused-but-set-global` when supported).

In `build.sh`:
- Add `Darwin*)` case in `case $(uname -s)`:
  - Check tools (`cmake`, `ninja`, `pkg-config`, `glslang`, `nasm`, `python3`, `git`, `lipo`).
  - Resolve `moltenvk` from `BB_MOLTENVK`, `out/moltenvk/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib`, `../theirs/bloodborne_pc/out/moltenvk/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib`, `../../theirs/bloodborne_pc/out/moltenvk/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib`, or `$VULKAN_SDK/lib/libMoltenVK.dylib`; verify `lipo -archs "$moltenvk" | grep -q x86_64`.
  - Resolve `vcpkg_root` from `VCPKG_ROOT`, `out/vcpkg`, or `/Users/felipe.dos.santos/code/theirs/bloodborne_pc/out/vcpkg` (bootstrapping if needed).
  - Configure CMake with `PKG_CONFIG_ALL_STATIC=1`, `-DCMAKE_OSX_ARCHITECTURES=x86_64`, `-DVCPKG_TARGET_TRIPLET=x64-osx`, `-DVCPKG_MANIFEST_DIR="$PWD/packaging/macos"`, `-DVCPKG_INSTALLED_DIR="$PWD/out/vcpkg_installed"`.
  - Build `bb-probe` and `bb-gpu-capabilities` (plus test targets when `--test` is passed), copy `libvulkan.*dylib`, `libMoltenVK.dylib`, and `MoltenVK_icd.json` to `out/`.

- [ ] **Step 4: Commit Task 1**

```bash
git add packaging/macos/vcpkg.json gpu/shim/macos/bb_mac_compat.h gpu/CMakeLists.txt build.sh
git commit -m "build(macos): add vcpkg manifest, CMake Apple branch, and build.sh Darwin target"
```

---

### Task 2: POSIX Compatibility Layer, Guest TLS, and Signal Stacks (`src/compat_mac.*`, `src/runtime*.c`, `tests/test_compat_mac.c`)

**Files:**
- Modify: `src/compat_mac.h`, `src/compat_mac.c`
- Modify: `src/runtime.h:5-18`, `:72-91`
- Modify: `src/runtime_sema.c:39-43`
- Modify: `src/runtime_thread.c:20-26`, `:74-130`
- Modify: `src/runtime_sotc.c:12-18`, `:63-69`, `:190-202`, `:316-331`, `:470-506`
- Create: `tests/test_compat_mac.c`

**Interfaces:**
- Consumes: `gpu/CMakeLists.txt` (`compat-mac-test` target from Task 1).
- Produces:
  - `src/compat_mac.h` / `src/compat_mac.c`: `compat_getrandom`, `compat_getrusage`, `compat_clock_nanosleep`, `compat_mutex_timedlock`, `compat_rwlock_timedrdlock`, `compat_rwlock_timedwrlock`, `compat_condattr_setclock`, `compat_barrier_init`/`wait`/`destroy`, `st_atim`/`st_mtim`/`st_ctim`, `pthread_setname_np(thread, name)`.
  - `src/runtime.h`: `void *runtime_guest_alloc(size_t size)`, `void runtime_guest_free(void *block)`, `uintptr_t runtime_sotc_heap(void)`, `void runtime_sotc_set_heap(uintptr_t heap)`.
  - `src/runtime_thread.c`: `uint32_t runtime_thread_tcb_offset(void)` returning `8u * (uint32_t)tls_key`, `set_gs(void *base)` calling `pthread_setspecific(tls_key, base)`, and `attach(GuestThread *t)` installing a 512 KiB `sigaltstack` on `__APPLE__`.

- [ ] **Step 1: Write `tests/test_compat_mac.c`**

Create `tests/test_compat_mac.c` asserting:
- `pthread_mutex_timedlock` times out with `ETIMEDOUT` after ~50 ms and succeeds on an unlocked mutex.
- `pthread_barrier_init`/`wait`/`destroy` synchronizes 2 threads.
- `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)` sleeps until the deadline.
- `getrandom` fills 300 bytes with non-zero entropy; `getrusage(RUSAGE_THREAD, &usage)` returns `0`.
- On `__APPLE__`: setting a `pthread_key_t` value via `pthread_setspecific` and reading `movq %gs:(%1), %0` at `8 * key` returns the exact pointer, and `macos_version_ok` rejects `"14.7"` / `"15.3.1"` and accepts `"15.4"` / `"26.1"` / `"27.0.1"`.

- [ ] **Step 2: Implement `src/compat_mac.h`, `src/compat_mac.c`, `src/runtime.h`, `src/runtime_sema.c`, `src/runtime_thread.c`, `src/runtime_sotc.c`**

- Implement the polyfills in `src/compat_mac.h` and `src/compat_mac.c`.
- In `src/runtime.h`: include `"compat_mac.h"` in the `#else` branch of `#ifdef _WIN32`; declare `runtime_guest_alloc` / `runtime_guest_free` (mapped to `calloc`/`free` off `__APPLE__`) and `runtime_sotc_heap` / `runtime_sotc_set_heap`.
- In `src/runtime_sema.c`: use `#if defined(_WIN32) || defined(__APPLE__)` for `#define WAIT_CLOCK CLOCK_REALTIME`.
- In `src/runtime_thread.c`:
  - Add `#elif defined(__APPLE__)` TLS key creation with `pthread_once` (`runtime_thread_tcb_offset` returns `8u * (uint32_t)tls_key`).
  - Use `runtime_guest_alloc` / `runtime_guest_free` for `GuestThread`, `ThreadAttr`, and `tls_block_alloc`.
  - In `attach(GuestThread *t)`, on `#ifndef _WIN32`, allocate a 512 KiB alternate signal stack (`sigaltstack`) per thread so faults on 16–64 KiB SotC fiber stacks never overflow the fiber stack.
  - In `runtime_thread.c` and `runtime_sotc.c`, guard `<sys/syscall.h>` with `#ifndef __APPLE__` and use `pthread_threadid_np(NULL, &tid)` for `get_thread_id` on `__APPLE__`.
  - In `src/runtime_sotc.c`, allocate `AudioHandle` and `EventFlag` with `runtime_guest_alloc` and define `runtime_sotc_heap(void)` / `runtime_sotc_set_heap(uintptr_t)`.

- [ ] **Step 3: Build and run `compat-mac-test`**

Run: `ninja -C out/gpu compat-mac-test && out/gpu/compat-mac-test`
Expected: `compat_mac: PASS`

- [ ] **Step 4: Commit Task 2**

```bash
git add src/compat_mac.h src/compat_mac.c src/runtime.h src/runtime_sema.c src/runtime_thread.c src/runtime_sotc.c tests/test_compat_mac.c
git commit -m "feat(macos): add POSIX compat polyfills, pthread-key guest TLS, and fiber sigaltstack"
```

---

### Task 3: Guest Memory Pool, Range Reservations, and Low Arena (`src/runtime_memory.c`, `src/runtime_mutex.c`, `src/runtime_rwlock.c`, `src/runtime.c`, `tests/test_runtime.c`)

**Files:**
- Modify: `src/runtime_memory.c:40-46`, `:252-275`, `:377-400`, `:704-730`
- Modify: `src/runtime_mutex.c:26-62`, `:100-156`
- Modify: `src/runtime_rwlock.c:46-52`, `:138-144`
- Modify: `src/runtime.c:16-19`, `:50-75`
- Modify: `tests/test_runtime.c:328-356`

**Interfaces:**
- Consumes: `runtime_guest_alloc` / `runtime_guest_free` declarations in `src/runtime.h` (Task 2).
- Produces:
  - `runtime_memory_reserve(void)` on `__APPLE__`: reserves `[SYSTEM_LOW_MIN, SYSTEM_LOW_MAX)` (`0x700000000..0x800000000`), `[LOW_MIN, LOW_MAX)` (`0x800000000..0xfc0000000`), and `[USER_MIN, USER_MAX)` (`0x7000000000..0xfc00000000`) with `PROT_NONE`, `MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE`.
  - `pool(void)` on `__APPLE__`: `shm_open("/bb-guest-<pid>", O_RDWR | O_CREAT | O_EXCL, 0600)` (unlinked before and after open), `ftruncate(POOL_SIZE + FLEX_SPAN)`, `mmap(MAP_SHARED)`.
  - `runtime_guest_alloc(size_t)` / `runtime_guest_free(void *)` on `__APPLE__`: 64-byte aligned, zero-initialized low-memory arena in `[LOW_MIN, LOW_MAX)` (`< 1 TiB`).

- [ ] **Step 1: Add macOS memory and low-arena assertions to `tests/test_runtime.c`**

In `tests/test_runtime.c` `main()`:
- Call `runtime_memory_reserve()` at the start of `main()`.
- Under `#ifdef __APPLE__`, assert that:
  - `runtime_guest_alloc(100)` and `runtime_guest_alloc(70000)` return non-null, 64-byte-aligned, zero-initialized pointers `< (1ULL << 40)`.
  - `runtime_guest_free` + re-allocation reuses and re-zeroes the block.
  - `runtime_low_map(16384, PROT_READ | PROT_WRITE)` returns an address in `[0x800000000, 0xfc0000000)`.
  - Fixed guest mapping at `SYSTEM_LOW_MIN` (`0x7f0000000`, SotC's system-managed window) succeeds via `sceKernelReserveVirtualRange`.
  - `runtime_thread_tcb_offset()` called concurrently from 8 threads returns the same non-zero multiple of 8.

- [ ] **Step 2: Implement Apple memory backend and low arena in `src/runtime_memory.c`, `src/runtime_mutex.c`, `src/runtime_rwlock.c`, `src/runtime.c`**

- In `src/runtime_memory.c`:
  - Define `USER_MIN = UINT64_C(0x7000000000)` and `LOW_MAX = UINT64_C(0xfc0000000)` on `__APPLE__` (`USER_MIN = 0x1000000000` and `LOW_MAX = USER_MIN` elsewhere).
  - Implement `__APPLE__` `pool()`, `host_discard()` (`memset`), `runtime_memory_reserve()` (reserving `SYSTEM_LOW_MIN..SYSTEM_LOW_MAX`, `LOW_MIN..LOW_MAX`, and `USER_MIN..USER_MAX`), `host_unmap()` (re-reserving with `PROT_NONE`), `runtime_low_map()` (using `MAP_FIXED` inside `[LOW_MIN, LOW_MAX)`), and `runtime_guest_alloc()` / `runtime_guest_free()`.
- In `src/runtime_mutex.c`, `src/runtime_rwlock.c`, and `src/runtime.c` (`guest_tls_get_addr`), replace `malloc`/`calloc`/`free` of guest-visible objects with `runtime_guest_alloc`/`runtime_guest_free`.
- In `src/runtime.c`, include `<unistd.h>` so `_exit` is declared if needed.

- [ ] **Step 3: Commit Task 3**

```bash
git add src/runtime_memory.c src/runtime_mutex.c src/runtime_rwlock.c src/runtime.c tests/test_runtime.c
git commit -m "feat(macos): add shm guest pool, three-window reservation, and low handle arena"
```

---

### Task 4: Loader (`src/probe.c`) and SotC CPU Patches (`gpu/sotc/cpu_patches.cpp`) on macOS under Rosetta 2

**Files:**
- Modify: `src/probe.c:19-30`, `:54-85`, `:127-134`, `:425-607`, `:657-746`, `:835-928`, `:1028-1159`
- Modify: `gpu/sotc/cpu_patches.cpp:948`, `:2268`, `:2353`
- Modify: `tests/test_compat_mac.c`

**Interfaces:**
- Consumes: `runtime_memory_reserve()`, `runtime_thread_tcb_offset()`, `runtime_sotc_set_heap()` from Tasks 2–3; `bbgpu_run_window_loop()` from Task 5.
- Produces:
  - Sub-2 GiB `reserve_eboot_range()` and `allocate_image(size)` on `_WIN32 || __APPLE__`.
  - `rosetta_ok()` and `check_cpu()` in `src/probe.c`.
  - Ahead-of-time SSE4a (`EXTRQ`, `INSERTQ`, `MOVNTSS`, `MOVNTSD`) and red-zone patching enabled on `__APPLE__` in `gpu/sotc/cpu_patches.cpp`, plus `SIGILL` fallback to `sotc_handle_illegal(context)` in `src/probe.c`.
  - Main-thread window loop (`main` -> `pthread_create(&loader, ..., loader_thread, argv)` -> `bbgpu_run_window_loop()`) on `__APPLE__`.

- [ ] **Step 1: Add sub-2 GiB reservation test `test_eboot_reservation_below_2gb` to `tests/test_compat_mac.c`**

In `tests/test_compat_mac.c` under `#ifdef __APPLE__`:
- Add `-Wl,-pagezero_size,0x400000` to `compat-mac-test` link options in `gpu/CMakeLists.txt`.
- Test scanning `[0x400000, 0x80000000)` via `mach_vm_region` and reserving 256 MiB with `mmap(..., PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0)`, asserting the returned pointer `p != MAP_FAILED` and `(uintptr_t)p + (256ULL << 20) <= 0x80000000ULL`, then unmapping it.

- [ ] **Step 2: Run `compat-mac-test` to verify the sub-2 GiB reservation passes**

Run: `ninja -C out/gpu compat-mac-test && out/gpu/compat-mac-test`
Expected: `compat_mac: PASS`

- [ ] **Step 3: Update `src/probe.c` and `gpu/sotc/cpu_patches.cpp` for macOS**

In `src/probe.c`:
- Include `<sys/ucontext.h>`, `<sys/sysctl.h>`, `<mach/mach.h>`, `<mach/mach_vm.h>` on `__APPLE__` (guarding `<malloc.h>`, `<ucontext.h>`, `<sys/syscall.h>`, `<sys/uio.h>` with `#ifndef __APPLE__`).
- Define `CTX_RIP(uc)`, `CTX_RBP(uc)`, `CTX_RDI(uc)` for `__APPLE__` (`uc->uc_mcontext->__ss.__rip`, etc.) vs Linux (`uc->uc_mcontext.gregs[...]`).
- Implement `reserve_eboot_range(void)` and `allocate_image(size_t size)` for `__APPLE__` using `mach_vm_region` over `[EBOOT_MIN, EBOOT_LIMIT)` (`0x400000..0x80000000`) and `mmap(..., MAP_FIXED, ...)`.
- Prefix `enter_on_stack` with `_` in inline assembly on `__APPLE__` (`ASM_NAME("enter_on_stack")`).
- Add `macos_version_ok` / `rosetta_ok` / `check_cpu` (checking `sysctl.proc_translated` and `kern.osproductversion >= 15.4`).
- Enable `patch_tcb_loads` on `#if defined(_WIN32) || defined(__APPLE__)`.
- On `__APPLE__`, implement `install_heap_hook()` as a lock-free trampoline in the end of the trampoline buffer (before the `cmdline.bin` slot) that records the first 2,000 `rdi` heap pointers passed to `eboot + 0x495e0` and calls `choose_heap()` -> `runtime_sotc_set_heap(best)` without needing `INT3` traps.
- In `fault(int sig, siginfo_t *info, void *context)`:
  - Pass `SIGBUS` and `SIGSEGV` to `bbgpu_handle_fault(context, info->si_addr)`.
  - Pass `SIGILL` to `if (sig == SIGILL && image && sotc_handle_illegal(context)) return;`.
  - Register `SIGSEGV`, `SIGILL`, `SIGBUS` with `sa.sa_flags = SA_SIGINFO | SA_ONSTACK`.
- Guard Linux-only watchdog (`thread_dump`, `watchdog`, `process_vm_readv`) with `#if !defined(_WIN32) && !defined(__APPLE__)`.
- In `runtime_restart(void)`, close fds `3..getdtablesize()` on `__APPLE__`.
- Export `BB_GPU_USER_DIR` via `realpath` before `bbgpu_register_kernel()`.
- On `__APPLE__`, rename the loader entry to `static int probe_main(int argc, char **argv)` and in `main()` call `reserve_eboot_range()` and `runtime_memory_reserve()`, handle `--vulkan-only` directly if passed, spawn `loader_thread` with a 32 MiB stack, and call `bbgpu_run_window_loop()`.

In `gpu/sotc/cpu_patches.cpp`:
- Change `#if defined(_WIN32)` at line 948 to `#if defined(_WIN32) || defined(__APPLE__)` so `NeedsSse4aEmulation()`, `PatchShortSse4aInstructions()`, and `PatchRedZoneMemoryInstructions()` are active on macOS under Rosetta 2.
- Remove `#if !defined(__APPLE__)` in `PrePatchInstructions()` so long-form `EXTRQ`/`INSERTQ`/`MOVNTSS`/`MOVNTSD` instructions are patched ahead of time on macOS too.

- [ ] **Step 4: Commit Task 4**

```bash
git add src/probe.c gpu/sotc/cpu_patches.cpp gpu/CMakeLists.txt tests/test_compat_mac.c
git commit -m "feat(macos): port loader, sub-2GiB eboot reservation, and SSE4a CPU patches"
```

---

### Task 5: SDL3 Metal Window, Windowed Mode, Unlocked Mouse Cursor, and Vulkan Glue (`gpu/shim/*`, `gpu/shadps4/*`, `tests/test_pad.c`)

**Files:**
- Modify: `gpu/bbgpu.h`
- Modify: `gpu/shim/sdl_window.h:38-48`
- Modify: `gpu/shim/window.cpp:15-86`, `:162-190`
- Modify: `gpu/shim/bbgpu.cpp:218-315`
- Modify: `gpu/shim/bbport_overlay.cpp:26-30`
- Modify: `gpu/shim/bbport_process_memory.cpp:5-48`
- Modify: `gpu/shim/bbport_threads.h:10-65`
- Modify: `gpu/shim/bbport_write_log.cpp:9-20`, `:97-112`
- Modify: `gpu/shim/core/libraries/kernel/threads.h:18-63`
- Modify: `gpu/shadps4/common/path_util.cpp:92-98`
- Modify: `gpu/shadps4/video_core/amdgpu/liverpool.cpp:110-122`, `:1339-1346`
- Modify: `gpu/shadps4/video_core/amdgpu/liverpool.h:171-176`, `:264-269`
- Modify: `gpu/shadps4/video_core/amdgpu/pm4_cmds.h:761-768`
- Modify: `gpu/shadps4/video_core/renderer_vulkan/vk_instance.cpp:280-290`, `:516-522`
- Modify: `gpu/shadps4/video_core/renderer_vulkan/vk_platform.cpp:195-202`, `:261-272`, `:406-412`
- Modify: `gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp:3519-3525`
- Modify: `gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.h:159-165`
- Modify: `src/vulkan_smoke.c:9-15`
- Modify: `tools/gpu_capabilities.c:71-80`
- Modify: `tests/test_gpu_runtime_stubs.cpp`
- Modify: `tests/test_pad.c`

**Interfaces:**
- Consumes: `SDL3` (`<SDL3/SDL.h>`, `<SDL3/SDL_metal.h>`), `bb_mac_compat.h` (Task 1), `runtime_sotc_heap` (Task 2).
- Produces:
  - `extern "C" void bbgpu_run_window_loop(void)` in `gpu/bbgpu.h` and `gpu/shim/bbgpu.cpp`.
  - `WindowSDL` in `gpu/shim/sdl_window.h` and `gpu/shim/window.cpp`:
    - Uses `SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN = true` on `__APPLE__`, attaches `SDL_MetalView metal_view = SDL_Metal_CreateView(window)`, sets `window_info.type = WindowSystemType::Metal` and `window_info.render_surface = SDL_Metal_GetLayer(metal_view)`, and calls `SDL_Metal_DestroyView(metal_view)` in `~WindowSDL()`.
    - Opens in **windowed mode** by default (`BB_FULLSCREEN` unset or `'0'` -> `SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN = false`), clamping initial `width_` / `height_` to 90% of `SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable)` when windowed.
    - **Never locks the mouse cursor:** calls `SDL_SetWindowRelativeMouseMode(window, false)`, `SDL_SetWindowMouseGrab(window, false)`, and `SDL_ShowCursor()` on window creation, and never enables `SDL_SetWindowRelativeMouseMode` or `SDL_SetWindowMouseGrab` during `PollEvents()`.

- [ ] **Step 1: Add `test_window_unlocked_mouse` and `bbgpu_mouse_take` test to `tests/test_pad.c`**

In `tests/test_pad.c`:
- Add a test function `test_window_unlocked_mouse(void)` that creates an `SDL_Window` using the same windowed properties (`SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN = true`, `SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN = false`), verifies `(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) == 0`, verifies `SDL_GetWindowRelativeMouseMode(win) == false` and `SDL_GetWindowMouseGrab(win) == false`, pushes `SDL_EVENT_WINDOW_FOCUS_GAINED` and `SDL_EVENT_MOUSE_BUTTON_DOWN` events, and verifies that the cursor remains unlocked (`!SDL_GetWindowRelativeMouseMode(win) && !SDL_GetWindowMouseGrab(win)`) and `pad_read_state` leaves `right_x == 128 && right_y == 128`.

- [ ] **Step 2: Implement SDL3 Metal window, windowed mode, unlocked mouse cursor, and Apple GPU shims**

- In `gpu/shim/sdl_window.h`: add `void* metal_view{};` under `#ifdef __APPLE__`.
- In `gpu/shim/window.cpp`:
  - Include `<SDL3/SDL_metal.h>` on `#ifdef __APPLE__`.
  - In `WindowSDL::WindowSDL(s32 width_, s32 height_, const char* title)`:
    - Check `const bool is_fullscreen = fullscreen && fullscreen[0] == '1';`.
    - When `!is_fullscreen`, query `SDL_Rect usable{};` via `SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable)` and if `usable.w > 0 && usable.h > 0 && (width_ > usable.w * 9 / 10 || height_ > usable.h * 9 / 10)`, scale `width_` and `height_` by `std::min(usable.w * 0.9 / width_, usable.h * 0.9 / height_)`.
    - Set `SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN` to `is_fullscreen`.
    - On `#ifdef __APPLE__`, set `SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN` to `true` (and `SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN` to `true` on non-Apple).
    - After `SDL_CreateWindowWithProperties(props)`, explicitly call:
      ```cpp
      SDL_SetWindowRelativeMouseMode(window, false);
      SDL_SetWindowMouseGrab(window, false);
      SDL_ShowCursor();
      ```
    - On `#ifdef __APPLE__`, when `driver` is `"cocoa"`, create `metal_view = SDL_Metal_CreateView(window)`, set `window_info.type = WindowSystemType::Metal`, and `window_info.render_surface = SDL_Metal_GetLayer(metal_view)`.
  - In `WindowSDL::~WindowSDL()`: on `#ifdef __APPLE__`, call `if (metal_view) SDL_Metal_DestroyView(metal_view);` before `SDL_DestroyWindow(window)`.
  - In `WindowSDL::PollEvents()`: remove the `SDL_SetWindowRelativeMouseMode(window, true)` mouse-locking block so clicking or focusing the window never locks or hides the cursor (`g_mouse_captured` remains `false`).
- In `gpu/bbgpu.h` and `gpu/shim/bbgpu.cpp`: add `bbgpu_run_window_loop(void)` and main-thread window creation handoff on `__APPLE__`. Also update `AddressSpace::GetUsableRegions()` on `__APPLE__` to `0x70'0000'0000ULL..0xfc'0000'0000ULL`.
- Apply the Apple portability enumeration, Mach-O `.incbin`, `pm4_cmds.h` `Address()`, `liverpool` / `vk_rasterizer` `gettid()`, `vk_instance.cpp` `robustness2`, `path_util.cpp`, `bbport_threads.h`, `bbport_process_memory.cpp`, `bbport_write_log.cpp`, `threads.h`, `src/vulkan_smoke.c`, `tools/gpu_capabilities.c`, and `tests/test_gpu_runtime_stubs.cpp` updates.

- [ ] **Step 3: Build `bb-probe`, `bb-gpu-capabilities`, and run all C tests and Vulkan smoke checks**

Run:
```bash
bash build.sh --test
DYLD_LIBRARY_PATH=$PWD/out VK_DRIVER_FILES=$PWD/out/MoltenVK_icd.json out/bb-probe --vulkan-only
DYLD_LIBRARY_PATH=$PWD/out VK_DRIVER_FILES=$PWD/out/MoltenVK_icd.json out/bb-gpu-capabilities --live-resolution
```
Expected:
- `Built .../out/bb-probe`
- All C tests (`compat-mac-test`, `pad-test`, `file-mods-test`, `content-test`, `runtime-test`, `sema-test`) print `PASS`.
- `out/bb-probe --vulkan-only` prints `Vulkan: Apple M...; command submission + 4096-byte readback PASS`.

- [ ] **Step 4: Commit Task 5**

```bash
git add gpu/ src/vulkan_smoke.c tools/gpu_capabilities.c tests/test_gpu_runtime_stubs.cpp tests/test_pad.c
git commit -m "feat(macos): add SDL3 Metal window in windowed mode with unlocked mouse cursor"
```

---

### Task 6: KosmicKrisp Vulkan Driver, `sotc.py` / `run.sh`, Cross-Platform Launcher, and Application Run

**Files:**
- Create: `tools/build_kosmickrisp.sh`
- Create: `launcher/sotc-launcher.sh`
- Create: `tests/test_launcher_platform.py`
- Modify: `sotc.py:52-63`, `:172-194`
- Modify: `run.sh:4-30`
- Modify: `launcher/sotc_launcher_win.py:24-110`, `:300-314`, `:546-556`, `:913-918`, `:999-1021`, `:1396-1406`, `:1482-1586`
- Modify: `launcher/sotc_discovery.py:184-211`

**Interfaces:**
- Consumes: `out/bb-probe` and `out/bb-gpu-capabilities` from Task 5.
- Produces:
  - `tools/build_kosmickrisp.sh`: produces `out/libvulkan_kosmickrisp.dylib`, `out/libz.1.dylib`, and `out/kosmickrisp_mesa_icd.json` (reusing `/Users/felipe.dos.santos/code/theirs/bloodborne_pc/out/kosmickrisp-build` when present, or building from `shadexternals/mesa-kosmickrisp` commit `af7dd0a35c071cecbfff32662675641d19e8e4db`).
  - `sotc.py`: cross-platform Shadow of the Colossus runner setting `BB_FULLSCREEN=0`, `SOTC_MOUSE=0`, `DYLD_LIBRARY_PATH`, and `VK_DRIVER_FILES` (KosmicKrisp preferred over MoltenVK).
  - `launcher/sotc_launcher_win.py` & `launcher/sotc-launcher.sh`: cross-platform Tkinter launcher with `APP_DEFAULTS['fullscreen'] = False` and `APP_DEFAULTS['mouse'] = False`.
  - `tests/test_launcher_platform.py`: headless unit tests for `sotc_launcher_win.py` on macOS.

- [ ] **Step 1: Write `tests/test_launcher_platform.py` and verify it fails**

Create `tests/test_launcher_platform.py` testing:
1. `launcher.run_command()` returns `[sys.executable, str(ROOT / 'sotc.py')]`.
2. `launcher.APP_DEFAULTS['fullscreen']` is `False` and `launcher.APP_DEFAULTS['mouse']` is `False`.
3. `launcher.game_environment(launcher.APP_DEFAULTS)` sets `BB_FULLSCREEN == '0'` and `SOTC_MOUSE == '0'`.
4. `launcher.CONFIG_DIR` on `darwin` is under `~/Library/Application Support/sotc-launcher`.
5. `GameJob(process, kill_after=1.0).terminate()` terminates the entire process group started with `start_new_session=True`, including grandchildren and processes that ignore `SIGTERM`.

Run: `python3 tests/test_launcher_platform.py`
Expected: FAIL (`AttributeError: module 'ctypes' has no attribute 'windll'` or `CONFIG_DIR` / `APP_DEFAULTS['mouse']` mismatch).

- [ ] **Step 2: Update `sotc.py`, `run.sh`, `launcher/sotc_launcher_win.py`, `launcher/sotc_discovery.py`, `launcher/sotc-launcher.sh`, and `tools/build_kosmickrisp.sh`**

- Create `tools/build_kosmickrisp.sh` (building x86-64 `libvulkan_kosmickrisp.dylib`, `libz.1.dylib`, and `kosmickrisp_mesa_icd.json` into `out/`, reusing an existing build in `../theirs/bloodborne_pc/out/kosmickrisp-build` if present).
- In `sotc.py`:
  - Guard `no_console()` with `if sys.platform != 'win32': return 0`.
  - Update `find_probe()` to look for `bb-probe` on POSIX and `bb-probe.exe` on Windows in `bin/` and `out/`.
  - Default `env.setdefault('BB_FULLSCREEN', '0')` and `env.setdefault('SOTC_MOUSE', '0')`.
  - On `sys.platform == 'darwin'`, set `DYLD_LIBRARY_PATH` to include `probe.parent`, and set `VK_DRIVER_FILES` (if unset) to `probe.parent / 'kosmickrisp_mesa_icd.json'` when present, else `probe.parent / 'MoltenVK_icd.json'`.
- In `run.sh`: add the `Darwin` `DYLD_LIBRARY_PATH` and `VK_DRIVER_FILES` setup at the top and skip `nix-shell` re-entry on `Darwin`.
- In `launcher/sotc_launcher_win.py`:
  - Add `WINDOWS = sys.platform == 'win32'`.
  - Set `CONFIG_DIR` to `~/Library/Application Support/sotc-launcher` on `darwin`, `APPDATA/sotc-launcher` on `win32`, `XDG_CONFIG_HOME/sotc-launcher` elsewhere.
  - Set `APP_DEFAULTS['fullscreen'] = False` and `APP_DEFAULTS['mouse'] = False`.
  - Guard `attach_stdio()`, `SetProcessDpiAwareness`, and the Desktop Shortcut button with `if WINDOWS:`.
  - Implement `system_language()` using `locale` on non-Windows.
  - Update `detect_gpu()` to find `bb-gpu-capabilities` (without `.exe` off Windows) and set `DYLD_LIBRARY_PATH` and `VK_DRIVER_FILES` on `darwin`.
  - Update `open_path()` to use `open` on `darwin` and `xdg-open` on Linux.
  - Pass `start_new_session=not WINDOWS` in `play()` and `play_without_window()`, and update `GameJob` to send `SIGTERM` then `SIGKILL` to the process group via `os.killpg` on POSIX.
- In `launcher/sotc_discovery.py`: update `scan()` to include `home / 'Library' / 'Application Support' / 'shadPS4'` on macOS and avoid scanning `/` recursively on POSIX (scan `extra_roots` and `likely` user directories instead).
- Create `launcher/sotc-launcher.sh` to find a Python with `tkinter` (`python3.13`, `python3.14`, `python3`, `/opt/homebrew/bin/python3.13`) and launch `launcher/sotc_launcher_win.py`.

- [ ] **Step 3: Run `tests/test_launcher_platform.py` and build/install KosmicKrisp into `out/`**

Run:
```bash
python3 tests/test_launcher_platform.py
bash tools/build_kosmickrisp.sh
DYLD_LIBRARY_PATH=$PWD/out VK_DRIVER_FILES=$PWD/out/kosmickrisp_mesa_icd.json out/bb-probe --vulkan-only
DYLD_LIBRARY_PATH=$PWD/out VK_DRIVER_FILES=$PWD/out/kosmickrisp_mesa_icd.json out/bb-gpu-capabilities --live-resolution
```
Expected:
- `tests/test_launcher_platform.py` prints `OK`.
- `out/libvulkan_kosmickrisp.dylib`, `out/libz.1.dylib`, and `out/kosmickrisp_mesa_icd.json` exist in `out/`.
- `out/bb-probe --vulkan-only` and `out/bb-gpu-capabilities --live-resolution` succeed on KosmicKrisp.

- [ ] **Step 4: Run the application on Apple Silicon in windowed mode with the mouse cursor unlocked**

Run:
- Extract or locate the CUSA08809 game directory (if not yet extracted from `/Users/felipe.dos.santos/Downloads/Shadow of the Colossus - EP9000-CUSA08809_00-SOTC0000000000EU-A0100-V0100.pkg`, extract with `/Users/felipe.dos.santos/code/theirs/bloodborne_pc/out/pkgtool/PkgTool.Core pkg_extract` into `../CUSA08809`), then launch via `python3 sotc.py` (and verify `bash launcher/sotc-launcher.sh`).
Expected:
- `sotc.py` prepares `out/boot-linked.bin`, `out/content.bin`, `out/sotc-cpu.bin`, `out/patches.bin`, `out/cmdline.bin`.
- `out/bb-probe` starts under Rosetta 2, logs `Image at 0x... (below 2 GiB, as the game's 31-bit pointers need)`, `CPU: x86-64 under Rosetta 2`, `GPU: window and Vulkan presenter ready`, `CPU patches: SSE4a emulated`, and opens the SDL3 Metal window in **windowed mode** with the **mouse cursor unlocked**.

- [ ] **Step 5: Commit Task 6**

```bash
git add tools/build_kosmickrisp.sh sotc.py run.sh launcher/sotc_launcher_win.py launcher/sotc_discovery.py launcher/sotc-launcher.sh tests/test_launcher_platform.py
git commit -m "feat(macos): wire KosmicKrisp, sotc.py, and cross-platform launcher for Apple Silicon"
```
