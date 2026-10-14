// SPDX-License-Identifier: GPL-2.0-or-later
// bbport (macOS): small POSIX helpers the renderer uses for thread identity. Force-included into
// the GPU library on Apple only (CMakeLists.txt), like bb_win_compat.h on Windows.
#pragma once
#ifdef __APPLE__
#include <pthread.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// No gettid on macOS: the Mach thread id, as Activity Monitor and lldb show it.
static inline int gettid(void) { uint64_t id = 0; pthread_threadid_np(NULL, &id); return (int)id; }
#ifdef __cplusplus
}
#endif
#endif
