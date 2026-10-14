/* macOS (x86-64 under Rosetta 2) counterparts of the few Linux calls the runtime uses. Included
 * through runtime.h after the C library headers, like compat_win.h on Windows, so the macros
 * below only replace calls in this project's own sources. */
#ifndef BB_COMPAT_MAC_H
#define BB_COMPAT_MAC_H
#ifdef __APPLE__
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>

#ifndef CLOCK_REALTIME_COARSE
#define CLOCK_REALTIME_COARSE CLOCK_REALTIME
#endif
#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP PTHREAD_RECURSIVE_MUTEX_INITIALIZER
#endif
/* struct stat keeps its timespecs under other names. */
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec
/* kern.arandom: getentropy, 256 bytes per call. */
ssize_t compat_getrandom(void *buffer, size_t size, unsigned flags);
#define getrandom compat_getrandom
/* No RUSAGE_THREAD: the calling thread's CPU time comes from Mach. */
#define RUSAGE_THREAD 1
int compat_getrusage(int who, struct rusage *out);
#define getrusage compat_getrusage
/* No clock_nanosleep: an absolute CLOCK_MONOTONIC deadline through nanosleep. Returns 0 once
 * the deadline has passed, EINTR when a signal cut the sleep short (callers loop). */
#define TIMER_ABSTIME 1
int compat_clock_nanosleep(clockid_t clock, int flags, const struct timespec *deadline, struct timespec *remain);
#define clock_nanosleep compat_clock_nanosleep
/* No timed mutex/rwlock locks. Deadlines are CLOCK_REALTIME, as the Linux calls take them.
 * ponytail: trylock polled every 50 us until the deadline; a condition-variable wait if a
 * profile shows contention here. */
int compat_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *deadline);
int compat_rwlock_timedrdlock(pthread_rwlock_t *lock, const struct timespec *deadline);
int compat_rwlock_timedwrlock(pthread_rwlock_t *lock, const struct timespec *deadline);
#define pthread_mutex_timedlock compat_mutex_timedlock
#define pthread_rwlock_timedrdlock compat_rwlock_timedrdlock
#define pthread_rwlock_timedwrlock compat_rwlock_timedwrlock
/* Condition variables wait on CLOCK_REALTIME only (runtime_sema.c picks that clock on Apple). */
static inline int compat_condattr_setclock(pthread_condattr_t *attr, clockid_t clock) { (void)attr; return clock == CLOCK_REALTIME ? 0 : EINVAL; }
#define pthread_condattr_setclock compat_condattr_setclock
/* Thread names: one-argument form, the calling thread only. */
#define pthread_setname_np(thread, name) pthread_setname_np(name)
/* The eboot reservation: the first free 256 MiB below 2 GiB (Shadow of the Colossus packs static
 * data pointers into 31 bits), claimed once per process. probe.c calls this from main() and again
 * from probe_main(), so repeat calls must return the same block, not a fresh one. */
void *compat_reserve_eboot_range(void);
/* Barriers (tests/test_runtime.c). */
typedef struct { pthread_mutex_t lock; pthread_cond_t wake; unsigned count, waiting, generation; } compat_barrier_t;
#define pthread_barrier_t compat_barrier_t
#define PTHREAD_BARRIER_SERIAL_THREAD (-1)
int compat_barrier_init(compat_barrier_t *barrier, const void *attr, unsigned count);
int compat_barrier_wait(compat_barrier_t *barrier);
int compat_barrier_destroy(compat_barrier_t *barrier);
#define pthread_barrier_init compat_barrier_init
#define pthread_barrier_wait compat_barrier_wait
#define pthread_barrier_destroy compat_barrier_destroy
#endif
#endif
