/* macOS (x86-64 under Rosetta 2) counterparts of the few Linux calls the runtime uses. Included
 * through runtime.h after the C library headers, like compat_win.h on Windows, so the macros
 * below only replace calls in this project's own sources. */
#ifndef BB_COMPAT_MAC_H
#define BB_COMPAT_MAC_H
#ifdef __APPLE__
#include <time.h>
#ifndef CLOCK_REALTIME_COARSE
#define CLOCK_REALTIME_COARSE CLOCK_REALTIME
#endif
#endif
#endif
