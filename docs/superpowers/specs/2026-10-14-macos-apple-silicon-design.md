# Shadow of the Colossus port on Apple Silicon (macOS) — design

Date: 2026-10-14.

## Goal

Build and run the **Shadow of the Colossus** native port (`ShadowOfTheColossus-PC`, CUSA08809 EU 1.01) on Apple Silicon macOS using SDL3 in **windowed mode** while **never locking or grabbing the mouse cursor** to the application window.

- `bash build.sh` on an Apple Silicon Mac produces an x86-64 `out/bb-probe` and `out/bb-gpu-capabilities` that run under Rosetta 2, plus the C test binaries (`bash build.sh --test`).
- `python3 sotc.py` (and `bash run.sh` / `bash launcher/sotc-launcher.sh`) prepares the game image and opens the SDL3 Metal-backed window in resizable windowed mode (`BB_FULLSCREEN=0`), scaled to fit within 90% of usable screen bounds when the configured output resolution exceeds the display.
- The mouse cursor remains visible and free to leave the window at all times (`SDL_GetWindowRelativeMouseMode(window) == false` and `SDL_GetWindowMouseGrab(window) == false`); clicking or focusing the window never calls `SDL_SetWindowRelativeMouseMode(window, true)` or `SDL_SetWindowMouseGrab(window, true)`.

## Why x86-64 under Rosetta 2

Shadow of the Colossus (PS4) is an x86-64 executable whose code runs directly in the host process without a CPU emulator. On Apple Silicon, Rosetta 2 translates the x86-64 process (loader, HLE runtime, static GPU library, and RWX guest code), matching how `bloodborne_pc` and `shadPS4` run on Apple Silicon.

## Key Technical Requirements & Differences from `bloodborne_pc`

1. **Sub-2 GiB eboot image reservation (`src/probe.c`, `gpu/CMakeLists.txt`):**
   - Shadow of the Colossus packs pointers to static data inside the eboot into 31 bits (`< 0x80000000`, 2 GiB).
   - On macOS x86-64, Mach-O binaries default to a 4 GiB `__PAGEZERO` (`0x0..0x100000000`). Linking `bb-probe` with `-Wl,-pagezero_size,0x400000` shrinks `__PAGEZERO` to 4 MiB and leaves ~1.7 GiB free below 2 GiB.
   - `reserve_eboot_range()` runs first thing in `main()`, walks `[0x400000, 0x80000000)` via `mach_vm_region`, and reserves a 256 MiB region (`PROT_NONE`, `MAP_FIXED | MAP_NORESERVE`) that `allocate_image()` commits when loading `boot-linked.bin` + the 16 MiB CPU patch trampoline area.

2. **Guest virtual memory layout (`src/runtime_memory.c`):**
   - `[0x0700000000, 0x0800000000)` (`SYSTEM_LOW_MIN..SYSTEM_LOW_MAX`, 28–32 GiB): reserved `PROT_NONE` for SotC's fixed mappings at `0x7f0000000`.
   - `[0x0800000000, 0x0fc0000000)` (`LOW_MIN..LOW_MAX`, 32–63 GiB, below the 63 GiB commpage): reserved `PROT_NONE` for `runtime_low_map` (stacks, import traps) and the `runtime_guest_alloc` low arena (guest-visible thread, TLS, mutex, rwlock, event-flag, and audio handles that must fit 40-bit packed pointers).
   - `[0x7000000000, 0xfc00000000)` (`USER_MIN..USER_MAX`, 448 GiB–1008 GiB, above the macOS `[64 GiB, 448 GiB)` kernel guard region): reserved `PROT_NONE` for direct and flexible guest memory mappings backed by a `shm_open` shared-memory pool.

3. **SSE4a emulation and fiber signal stacks under Rosetta 2 (`gpu/sotc/cpu_patches.cpp`, `src/probe.c`, `src/runtime_thread.c`):**
   - Rosetta 2 supports AVX, AVX2, FMA, BMI1, LZCNT, and MOVBE (since macOS 15.4), but does **not** support AMD-only SSE4a (`EXTRQ`, `INSERTQ`, `MOVNTSS`, `MOVNTSD`).
   - `gpu/sotc/cpu_patches.cpp` enables `NeedsSse4aEmulation()`, `PatchShortSse4aInstructions()`, `PatchRedZoneMemoryInstructions()`, and `PrePatchInstructions()` on `__APPLE__` (as on `_WIN32`), rewriting 4-byte and long SSE4a instructions into the 16 MiB trampoline area at load time, with `SIGILL` -> `sotc_handle_illegal(context)` as the runtime fallback.
   - SotC runs its jobs on fibers with 16–64 KiB stacks packed back-to-back without guard pages. Each attached guest thread installs a 512 KiB `sigaltstack` and `sigaction` uses `SA_SIGINFO | SA_ONSTACK` so `SIGBUS`/`SIGSEGV` page-tracking faults run on the alternate stack without overflowing fiber stacks or clobbering the 128-byte System V red zone.

4. **SDL3 Metal Window on the Main Thread, Windowed Mode, and Unlocked Mouse Cursor (`gpu/shim/window.cpp`, `gpu/shim/sdl_window.h`, `gpu/shim/bbgpu.cpp`, `src/probe.c`):**
   - SDL3's Cocoa backend requires window creation and event pumping on the process main thread. `main()` spawns the loader and guest entry on a 32 MiB pthread and runs `bbgpu_run_window_loop()` on the main thread.
   - `WindowSDL` creates the window via `SDL_CreateWindowWithProperties` with:
     - `SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN = true`
     - `SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN = true`
     - `SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN = false` by default (`BB_FULLSCREEN` unset or `'0'`).
   - When windowed, if the requested output dimensions exceed 90% of `SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable)`, the initial window size is scaled down to fit within 90% of the usable desktop while preserving aspect ratio.
   - `SDL_Metal_CreateView(window)` and `SDL_Metal_GetLayer(metal_view)` provide the `CAMetalLayer*` for `VK_EXT_metal_surface`; `~WindowSDL()` calls `SDL_Metal_DestroyView(metal_view)` before `SDL_DestroyWindow(window)`.
   - **Mouse cursor never locked:**
     - `WindowSDL` explicitly sets `SDL_SetWindowRelativeMouseMode(window, false)`, `SDL_SetWindowMouseGrab(window, false)`, and `SDL_ShowCursor()` on creation.
     - `WindowSDL::PollEvents()` does **not** call `SDL_SetWindowRelativeMouseMode(window, true)` or `SDL_SetWindowMouseGrab(window, true)` on mouse clicks or focus gain.
     - `SOTC_MOUSE` defaults to `'0'` across `window.cpp`, `sotc.py`, and `launcher/sotc_launcher_win.py` (`APP_DEFAULTS['mouse'] = False`, `APP_DEFAULTS['fullscreen'] = False`).

5. **Vulkan Driver (`tools/build_kosmickrisp.sh`, `sotc.py`, `run.sh`, `launcher/sotc_launcher_win.py`):**
   - MoltenVK is used for `bb-probe --vulkan-only` and `bb-gpu-capabilities` fallback, while Mesa's x86-64 **KosmicKrisp** (`libvulkan_kosmickrisp.dylib` + `kosmickrisp_mesa_icd.json`) is used to run the game renderer (which requires **buffer device address** -- `vkGetBufferDeviceAddress` returns 0 on MoltenVK and the renderer stops in `buffer_cache/buffer.cpp`; `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=1` does not change that. Amended 2026-10-14: the original text said "sparse buffer binding, `robustBufferAccess2`, and `nullDescriptor`"; on macOS robustness2 is optional, as `vk_instance.cpp` only warns about it, and the actual missing feature is buffer device address).
   - `sotc.py`, `run.sh`, and `launcher/sotc_launcher_win.py` automatically set `DYLD_LIBRARY_PATH` to `out/` and select `kosmickrisp_mesa_icd.json` when present (falling back to `MoltenVK_icd.json`).
